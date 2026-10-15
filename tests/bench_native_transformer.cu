// Native-only Transformer numerical checks and isolated CUDA timings.
#include "../src/native_transformer.cuh"
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>

namespace nt = puf_transformer;
void emit(const std::string& record, int output_fd);

// Deliberately independent FP64 CPU reference for small correctness tests.
// It has a plain row-major loop implementation, no CUDA/BLAS dependency.
struct Reference {
    nt::Config c;
    std::vector<std::vector<double>> parameters;
    std::vector<double> x, upstream;
    explicit Reference(nt::Model<float>& model) : c(model.config) {
        for (auto* p : model.parameters()) {
            auto v = p->value.download();
            parameters.emplace_back(v.begin(), v.end());
        }
        auto input = model.input.download(), grad = model.upstream.download();
        x.assign(input.begin(), input.end()); upstream.assign(grad.begin(), grad.end());
    }
    std::vector<double> norm(const std::vector<double>& input, size_t gamma, size_t beta) const {
        std::vector<double> y(input.size());
        for (int r = 0; r < c.batch * c.tokens; ++r) {
            double sum = 0, square = 0;
            for (int j = 0; j < c.width; ++j) {
                double v = input[r * c.width + j]; sum += v; square += v * v;
            }
            double mean = sum / c.width;
            double inverse = 1 / std::sqrt(std::max(square / c.width - mean * mean, 0.) + 1e-5);
            for (int j = 0; j < c.width; ++j)
                y[r * c.width + j] = (input[r * c.width + j] - mean) * inverse * parameters[gamma][j] + parameters[beta][j];
        }
        return y;
    }
    std::vector<double> linear(const std::vector<double>& input, size_t weight, size_t bias, int in, int out) const {
        int rows = c.batch * c.tokens;
        std::vector<double> y(size_t(rows) * out);
        for (int r = 0; r < rows; ++r) for (int j = 0; j < out; ++j) {
            double value = parameters[bias][j];
            for (int k = 0; k < in; ++k) value += input[r * in + k] * parameters[weight][j * in + k];
            y[r * out + j] = value;
        }
        return y;
    }
    std::vector<double> forward() const {
        int d = c.width, n = c.tokens, dk = d / c.heads;
        std::vector<double> current = x;
        for (int layer = 0; layer < c.layers; ++layer) {
            size_t p = size_t(layer) * 12;
            auto z = norm(current, p, p + 1);
            auto qkv = linear(z, p + 2, p + 3, d, 3 * d);
            std::vector<double> context(current.size(), 0.);
            for (int b = 0; b < c.batch; ++b) for (int h = 0; h < c.heads; ++h) {
                for (int query = 0; query < n; ++query) {
                    std::vector<double> probability(n);
                    for (int key = 0; key < n; ++key) {
                        double dot = 0.;
                        for (int j = 0; j < dk; ++j)
                            dot += qkv[(b * n + query) * 3 * d + h * dk + j] * qkv[(b * n + key) * 3 * d + d + h * dk + j];
                        probability[key] = dot / std::sqrt(double(dk));
                    }
                    double maximum = *std::max_element(probability.begin(), probability.end()), sum = 0.;
                    for (double& v : probability) { v = std::exp(v - maximum); sum += v; }
                    for (int key = 0; key < n; ++key) for (int j = 0; j < dk; ++j)
                        context[(b * n + query) * d + h * dk + j] += probability[key] / sum * qkv[(b * n + key) * 3 * d + 2 * d + h * dk + j];
                }
            }
            auto residual = linear(context, p + 4, p + 5, d, d);
            for (size_t i = 0; i < current.size(); ++i) residual[i] += current[i];
            z = norm(residual, p + 6, p + 7);
            auto hidden = linear(z, p + 8, p + 9, d, c.ff);
            for (double& v : hidden) v = .5 * v * (1. + std::tanh(.7978845608028654 * (v + .044715 * v * v * v)));
            current = linear(hidden, p + 10, p + 11, c.ff, d);
            for (size_t i = 0; i < current.size(); ++i) current[i] += residual[i];
        }
        return norm(current, size_t(c.layers) * 12, size_t(c.layers) * 12 + 1);
    }
    double loss() const {
        auto y = forward();
        double result = 0.;
        for (size_t i = 0; i < y.size(); ++i) result += y[i] * upstream[i];
        return result;
    }
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void numerical_check(nt::Config c, int output_fd) {
    nt::Model<float> model(c);
    model.forward(); model.backward();
    nt::check(cudaStreamSynchronize(model.stream), "numerical check synchronize");
    Reference reference(model);
    auto actual = model.output.download();
    auto expected = reference.forward();
    double forward_error = 0.;
    for (size_t i = 0; i < actual.size(); ++i) forward_error = std::max(forward_error, std::abs(actual[i] - expected[i]));
    require(forward_error < 2e-5, "FP64 CPU / FP32 GPU forward mismatch " + std::to_string(forward_error));
    double max_gradient_error = 0.;
    size_t checks = 0;
    auto compare = [&](double analytic, double numerical, const std::string& label) {
        double error = std::abs(analytic - numerical);
        max_gradient_error = std::max(max_gradient_error, error);
        require(std::isfinite(analytic) && error < 2e-4 + 2e-3 * std::abs(numerical),
                label + " gradient mismatch: GPU=" + std::to_string(analytic) + " finite_difference=" + std::to_string(numerical));
        ++checks;
    };
    constexpr double epsilon = 1e-4;
    auto parameters = model.parameters();
    for (size_t p = 0; p < parameters.size(); ++p) {
        auto gradients = parameters[p]->gradient.download();
        std::vector<size_t> positions = {0, gradients.size() / 3, gradients.size() / 2, gradients.size() - 1};
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
        for (size_t i : positions) {
            double original = reference.parameters[p][i];
            reference.parameters[p][i] = original + epsilon; double plus = reference.loss();
            reference.parameters[p][i] = original - epsilon; double minus = reference.loss();
            reference.parameters[p][i] = original;
            compare(gradients[i], (plus - minus) / (2 * epsilon), parameters[p]->name + "[" + std::to_string(i) + "]");
        }
    }
    auto input_gradient = model.state_gradient.download();
    for (size_t i = 0; i < reference.x.size(); i += std::max(size_t(1), reference.x.size() / 19)) {
        double original = reference.x[i];
        reference.x[i] = original + epsilon; double plus = reference.loss();
        reference.x[i] = original - epsilon; double minus = reference.loss();
        reference.x[i] = original;
        compare(input_gradient[i], (plus - minus) / (2 * epsilon), "input[" + std::to_string(i) + "]");
    }
    size_t count = 0;
    for (auto* p : parameters) count += p->value.count;
    require(count == c.parameter_count(), "parameter count formula mismatch");
    // A second backward overwrites, rather than accumulates, parameter gradients.
    auto previous = parameters[0]->gradient.download();
    model.backward(); nt::check(cudaStreamSynchronize(model.stream), "repeat backward synchronize");
    auto repeated = parameters[0]->gradient.download();
    for (size_t i = 0; i < previous.size(); ++i)
        require(std::abs(previous[i] - repeated[i]) < 2e-5f, "gradient accumulation across calls");
    std::ostringstream record;
    record << "{\"test\":\"fp64_reference_and_finite_difference\",\"passed\":true,\"batch\":" << c.batch
              << ",\"tokens\":" << c.tokens << ",\"width\":" << c.width << ",\"layers\":" << c.layers
              << ",\"heads\":" << c.heads << ",\"checked_derivatives\":" << checks
              << ",\"forward_max_abs\":" << forward_error << ",\"gradient_max_abs\":" << max_gradient_error << '}';
    emit(record.str(), output_fd);
}

template<class T> void check_finite(nt::Model<T>& model, bool execute = true) {
    if (execute) { model.forward(); model.backward(); }
    nt::check(cudaStreamSynchronize(model.stream), "finite check synchronize");
    for (float x : model.output.download()) require(std::isfinite(x), "nonfinite output");
    for (float x : model.state_gradient.download()) require(std::isfinite(x), "nonfinite input gradient");
    for (auto* p : model.parameters()) for (float x : p->gradient.download())
        require(std::isfinite(x), "nonfinite parameter gradient: " + p->name);
}

void bf16_check(int output_fd) {
    nt::Config c{2, 7, 32, 2, 4, 64};
    nt::Model<float> full(c);
    nt::Model<__nv_bfloat16> reduced(c);
    // Compare identical rounded inputs/parameters; differences then reflect
    // BF16 intermediate arithmetic, not differing initial conditions.
    auto a = full.parameters();
    auto b = reduced.parameters();
    for (size_t i = 0; i < a.size(); ++i) a[i]->value.upload(b[i]->value.download());
    full.input.upload(reduced.input.download());
    full.upstream.upload(reduced.upstream.download());
    check_finite(full); check_finite(reduced);
    auto relative = [](const std::vector<float>& x, const std::vector<float>& y) {
        double numerator = 0., denominator = 0.;
        for (size_t i = 0; i < x.size(); ++i) { numerator += double(x[i] - y[i]) * (x[i] - y[i]); denominator += double(x[i]) * x[i]; }
        return std::sqrt(numerator / std::max(denominator, 1e-20));
    };
    double forward = relative(full.output.download(), reduced.output.download());
    double backward = relative(full.state_gradient.download(), reduced.state_gradient.download());
    double weight_numerator = 0., weight_denominator = 0.;
    for (size_t i = 0; i < a.size(); ++i) {
        auto x = a[i]->gradient.download(), y = b[i]->gradient.download();
        for (size_t j = 0; j < x.size(); ++j) {
            weight_numerator += double(x[j] - y[j]) * (x[j] - y[j]);
            weight_denominator += double(x[j]) * x[j];
        }
    }
    double parameter = std::sqrt(weight_numerator / weight_denominator);
    require(forward < .025 && backward < .05 && parameter < .05, "BF16/FP32 comparison outside tolerance");
    std::ostringstream record;
    record << "{\"test\":\"bf16_vs_fp32\",\"passed\":true,\"forward_relative_l2\":" << forward
           << ",\"input_gradient_relative_l2\":" << backward << ",\"parameter_gradient_relative_l2\":" << parameter << '}';
    emit(record.str(), output_fd);
}

template<class T> void graph_check(const char* precision, int output_fd) {
    nt::Model<T> model({2, 7, 32, 2, 4, 64});
    check_finite(model);
    auto expected_output = model.output.download();
    auto expected_input_gradient = model.state_gradient.download();
    std::vector<std::vector<float>> expected_parameters;
    for (auto* p : model.parameters()) expected_parameters.push_back(p->gradient.download());
    cudaGraph_t graph;
    cudaGraphExec_t executable;
    nt::check(cudaStreamBeginCapture(model.stream, cudaStreamCaptureModeThreadLocal), "test begin capture");
    model.forward(); model.backward();
    nt::check(cudaStreamEndCapture(model.stream, &graph), "test end capture");
    nt::check(cudaGraphInstantiate(&executable, graph, nullptr, nullptr, 0), "test instantiate graph");
    // Poison the destinations so an omitted graph write cannot pass by reusing
    // an eager result. Multiple replays must overwrite, not accumulate, gradients.
    nt::check(cudaMemsetAsync(model.output.data, 0xff, model.output.count * sizeof(T), model.stream), "poison output");
    nt::check(cudaMemsetAsync(model.state_gradient.data, 0xff, model.state_gradient.count * sizeof(T), model.stream), "poison input gradient");
    for (auto* p : model.parameters())
        nt::check(cudaMemsetAsync(p->gradient.data, 0xff, p->gradient.count * sizeof(float), model.stream), "poison parameter gradient");
    for (int i = 0; i < 3; ++i) nt::check(cudaGraphLaunch(executable, model.stream), "test graph launch");
    check_finite(model, false);
    double maximum = 0.;
    auto compare = [&](const std::vector<float>& expected, const std::vector<float>& actual) {
        for (size_t i = 0; i < expected.size(); ++i) {
            double error = std::abs(double(expected[i]) - actual[i]);
            maximum = std::max(maximum, error);
            require(error < 2e-4 + .005 * std::abs(expected[i]), "CUDA graph/eager result mismatch");
        }
    };
    compare(expected_output, model.output.download());
    compare(expected_input_gradient, model.state_gradient.download());
    auto parameters = model.parameters();
    for (size_t i = 0; i < parameters.size(); ++i) compare(expected_parameters[i], parameters[i]->gradient.download());
    nt::check(cudaGraphExecDestroy(executable), "test destroy graph executable");
    nt::check(cudaGraphDestroy(graph), "test destroy graph");
    std::ostringstream record;
    record << "{\"test\":\"graph_vs_eager\",\"passed\":true,\"precision\":\"" << precision
           << "\",\"max_abs\":" << maximum << '}';
    emit(record.str(), output_fd);
}

struct Timing { double median_ms, min_ms, max_ms, median_wall_ms; };

Timing measure(cudaStream_t stream, const std::function<void()>& operation, int warmup, int iterations, int repeats, bool graphs) {
    for (int i = 0; i < warmup; ++i) operation();
    nt::check(cudaStreamSynchronize(stream), "warmup synchronize");
    cudaGraph_t graph = nullptr;
    cudaGraphExec_t executable = nullptr;
    if (graphs) {
        nt::check(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal), "begin capture");
        operation();
        nt::check(cudaStreamEndCapture(stream, &graph), "end capture");
        nt::check(cudaGraphInstantiate(&executable, graph, nullptr, nullptr, 0), "instantiate graph");
        for (int i = 0; i < warmup; ++i) nt::check(cudaGraphLaunch(executable, stream), "graph warmup");
        nt::check(cudaStreamSynchronize(stream), "graph warmup synchronize");
    }
    cudaEvent_t start, stop;
    nt::check(cudaEventCreate(&start), "create start event");
    nt::check(cudaEventCreate(&stop), "create stop event");
    std::vector<double> gpu, wall;
    for (int r = 0; r < repeats; ++r) {
        auto begin = std::chrono::steady_clock::now();
        nt::check(cudaEventRecord(start, stream), "start event");
        for (int i = 0; i < iterations; ++i) {
            if (graphs) nt::check(cudaGraphLaunch(executable, stream), "launch graph");
            else operation();
        }
        nt::check(cudaEventRecord(stop, stream), "stop event");
        nt::check(cudaEventSynchronize(stop), "event synchronize");
        auto end = std::chrono::steady_clock::now();
        float ms = 0.f;
        nt::check(cudaEventElapsedTime(&ms, start, stop), "event elapsed time");
        gpu.push_back(ms / iterations);
        wall.push_back(std::chrono::duration<double, std::milli>(end - begin).count() / iterations);
    }
    nt::check(cudaEventDestroy(start), "destroy start event");
    nt::check(cudaEventDestroy(stop), "destroy stop event");
    if (executable) nt::check(cudaGraphExecDestroy(executable), "destroy graph executable");
    if (graph) nt::check(cudaGraphDestroy(graph), "destroy graph");
    std::sort(gpu.begin(), gpu.end()); std::sort(wall.begin(), wall.end());
    return {gpu[gpu.size() / 2], gpu.front(), gpu.back(), wall[wall.size() / 2]};
}

struct Options {
    nt::Config shape;
    std::string precision = "fp32", output;
    int warmup = 5, iterations = 10, repeats = 5;
    bool graphs = true, tests = false, suite = false;
};

std::string timing_json(const char* name, const Timing& time, int batch, int tokens) {
    std::ostringstream out;
    out << std::setprecision(9) << '"' << name << "\":{\"median_ms\":" << time.median_ms
        << ",\"min_ms\":" << time.min_ms << ",\"max_ms\":" << time.max_ms
        << ",\"wall_ms\":" << time.median_wall_ms
        << ",\"observations_per_second\":" << batch * 1000. / time.median_ms
        << ",\"tokens_per_second\":" << double(batch) * tokens * 1000. / time.median_ms << '}';
    return out.str();
}

void emit(const std::string& record, int output_fd) {
    std::cout << record << std::endl;
    if (output_fd >= 0) {
        std::string line = record + "\n";
        size_t offset = 0;
        while (offset < line.size()) {
            ssize_t result = write(output_fd, line.data() + offset, line.size() - offset);
            if (result <= 0) throw std::runtime_error("failed writing results");
            offset += size_t(result);
        }
    }
}

template<class T> void benchmark(const Options& options, const cudaDeviceProp& device, int output_fd) {
    nt::Config c = options.shape; c.validate();
    size_t free_bytes = 0, total_bytes = 0;
    nt::check(cudaMemGetInfo(&free_bytes, &total_bytes), "query free memory");
    size_t m = size_t(c.batch) * c.tokens, md = m * c.width;
    size_t attention = size_t(c.batch) * c.heads * c.tokens * c.tokens;
    size_t owned_estimate = (14 * md + m * c.ff + attention) * sizeof(T) + 2 * m * sizeof(float)
        + c.layers * ((12 * md + 2 * m * c.ff + attention) * sizeof(T) + 4 * m * sizeof(float))
        + c.parameter_count() * (sizeof(T) + sizeof(float));
    if (owned_estimate + 256ULL * 1024 * 1024 > free_bytes) {
        std::ostringstream skip;
        skip << "{\"type\":\"skipped\",\"reason\":\"memory_headroom\",\"width\":" << c.width
             << ",\"layers\":" << c.layers << ",\"batch\":" << c.batch << ",\"tokens\":" << c.tokens
             << ",\"required_bytes\":" << owned_estimate << ",\"free_bytes\":" << free_bytes << '}';
        emit(skip.str(), output_fd);
        return;
    }
    nt::Model<T> model(c);
    check_finite(model);
    auto forward = measure(model.stream, [&] { model.forward(); }, options.warmup, options.iterations, options.repeats, options.graphs);
    auto backward = measure(model.stream, [&] { model.backward(); }, options.warmup, options.iterations, options.repeats, options.graphs);
    auto combined = measure(model.stream, [&] { model.forward(); model.backward(); }, options.warmup, options.iterations, options.repeats, options.graphs);
    // Verify replaying captured graphs still produces valid output/gradients.
    check_finite(model, false);
    std::ostringstream record;
    record << std::setprecision(9)
        << "{\"type\":\"transformer_backbone\",\"device\":\"" << device.name << "\",\"compute_capability\":\""
        << device.major << '.' << device.minor << "\",\"precision\":\"" << options.precision
        << "\",\"parameter_gradients\":\"fp32\",\"attention\":\"explicit_cublas_softmax\",\"graphs\":" << (options.graphs ? "true" : "false")
        << ",\"batch\":" << c.batch << ",\"tokens\":" << c.tokens << ",\"width\":" << c.width
        << ",\"layers\":" << c.layers << ",\"heads\":" << c.heads << ",\"ff\":" << c.ff
        << ",\"parameters\":" << c.parameter_count() << ",\"owned_cuda_bytes\":" << model.allocated_bytes
        << ",\"warmup\":" << options.warmup << ",\"iterations\":" << options.iterations << ",\"repeats\":" << options.repeats
        << ",\"finite_check\":true," << timing_json("forward", forward, c.batch, c.tokens) << ','
        << timing_json("backward", backward, c.batch, c.tokens) << ','
        << timing_json("forward_backward", combined, c.batch, c.tokens) << '}';
    emit(record.str(), output_fd);
}

int main(int argc, char** argv) {
    int output_fd = -1;
    try {
        Options options;
        bool ff_explicit = false, heads_explicit = false;
        auto value = [&](int& index) -> std::string {
            if (++index == argc) throw std::runtime_error("missing option value");
            return argv[index];
        };
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--test") options.tests = true;
            else if (arg == "--suite") options.suite = true;
            else if (arg == "--batch") options.shape.batch = std::stoi(value(i));
            else if (arg == "--tokens") options.shape.tokens = std::stoi(value(i));
            else if (arg == "--width") options.shape.width = std::stoi(value(i));
            else if (arg == "--layers") options.shape.layers = std::stoi(value(i));
            else if (arg == "--heads") { options.shape.heads = std::stoi(value(i)); heads_explicit = true; }
            else if (arg == "--ff") { options.shape.ff = std::stoi(value(i)); ff_explicit = true; }
            else if (arg == "--precision") options.precision = value(i);
            else if (arg == "--iterations") options.iterations = std::stoi(value(i));
            else if (arg == "--warmup") options.warmup = std::stoi(value(i));
            else if (arg == "--repeats") options.repeats = std::stoi(value(i));
            else if (arg == "--graphs") options.graphs = std::stoi(value(i)) != 0;
            else if (arg == "--output") options.output = value(i);
            else if (arg == "--help") {
                std::cout << "Native CUDA/cuBLAS Transformer microbenchmark; no Python.\n"
                    << "--test: FP64 CPU oracle and finite differences (plus BF16 comparison on supported GPUs).\n"
                    << "--suite: benchmark 128x4, 256x4, 256x6, 256x12 at the selected batch/tokens.\n"
                    << "--width 256 --layers 12 --heads 8 --ff 1024 --tokens 264 --batch 1\n"
                    << "--precision fp32|bf16 --graphs 0|1 --warmup 5 --iterations 10 --repeats 5\n"
                    << "--output NEW_FILE.jsonl (exclusive creation; never overwrites).\n"
                    << "No environment, tokenizer, positions, heads, loss, optimizer or transfers in the timed region.\n";
                return 0;
            } else throw std::runtime_error("unknown argument " + arg);
        }
        require(options.iterations > 0 && options.warmup > 0 && options.repeats > 0, "timing counts must be positive");
        require(options.precision == "fp32" || options.precision == "bf16", "precision must be fp32 or bf16");
        if (!ff_explicit) {
            require(options.shape.width > 0 && options.shape.width <= INT32_MAX / 4, "width out of range");
            options.shape.ff = 4 * options.shape.width;
        }
        if (!heads_explicit) options.shape.heads = options.shape.width == 128 ? 4 : 8;
        options.shape.validate();
        cudaDeviceProp device;
        nt::check(cudaGetDeviceProperties(&device, 0), "get GPU properties");
        require(options.precision != "bf16" || device.major >= 8, "BF16 benchmark requires compute capability 8.0 or newer");
        if (!options.output.empty()) {
            output_fd = open(options.output.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
            require(output_fd >= 0, "cannot exclusively create output " + options.output);
        }
        if (options.tests) {
            numerical_check({2, 5, 8, 2, 2, 16}, output_fd);
            numerical_check({1, 1, 8, 1, 1, 12}, output_fd);
            numerical_check({1, 9, 12, 2, 3, 19}, output_fd);
            graph_check<float>("fp32", output_fd);
            if (device.major >= 8) { bf16_check(output_fd); graph_check<__nv_bfloat16>("bf16", output_fd); }
            else emit("{\"test\":\"bf16_vs_fp32\",\"skipped\":\"GPU lacks BF16 support\"}", output_fd);
        } else {
            std::vector<std::pair<int, int>> shapes = options.suite
                ? std::vector<std::pair<int, int>>{{128, 4}, {256, 4}, {256, 6}, {256, 12}}
                : std::vector<std::pair<int, int>>{{options.shape.width, options.shape.layers}};
            for (auto shape : shapes) {
                Options selected = options;
                selected.shape.width = shape.first; selected.shape.layers = shape.second;
                if (options.suite) { selected.shape.heads = shape.first == 128 ? 4 : 8; selected.shape.ff = 4 * shape.first; }
                if (options.precision == "fp32") benchmark<float>(selected, device, output_fd);
                else benchmark<__nv_bfloat16>(selected, device, output_fd);
            }
        }
        if (output_fd >= 0) close(output_fd);
        return 0;
    } catch (const std::exception& e) {
        if (output_fd >= 0) close(output_fd);
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
