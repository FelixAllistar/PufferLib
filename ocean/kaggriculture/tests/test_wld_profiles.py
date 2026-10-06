import configparser
import shlex
from pathlib import Path
import subprocess
import sys
import unittest


SCRIPT = Path(__file__).parents[1] / "run.py"
MAIN = SCRIPT.parents[2] / "config/kaggriculture.ini"


class ProfileTests(unittest.TestCase):
    def command(self, profile, mode="train", *overrides):
        return shlex.split(subprocess.run([sys.executable, str(SCRIPT), mode,
            "--profile", profile, "--dry-run", *overrides], check=True,
            capture_output=True, text=True).stdout)

    def test_three_controlled_profiles_use_separate_binary_and_environment_handoff(self):
        for mode, profile in enumerate(("wld", "wld_global", "wld_paired")):
            with self.subTest(profile=profile):
                command = self.command(profile)
                self.assertEqual(command[:2], [f"KAG_CRITIC_MODE={mode}", "KAG_REWARD_WIN_LOSS_DRAW=1"])
                binary = next(i for i, option in enumerate(command) if not option.startswith("KAG_"))
                self.assertEqual(Path(command[binary]).name, "puffer_wld")
                self.assertEqual(command[binary + 1], "train")
                # Profiles select the critic, not editable training/reward settings.
                self.assertEqual(command[binary + 2:], [])
                self.assertFalse(any(option.startswith("--policy.critic_mode=")
                    or option.startswith("--env.reward_win_loss_draw=") for option in command))

    def test_eval_is_reset_free_and_explicit_overrides_win(self):
        command = self.command("wld", "eval", "--policy.critic_mode=2", "--base.eval_episodes=8")
        self.assertEqual(command[0], "KAG_CRITIC_MODE=2")
        self.assertIn("--env.reset_state_prob=0", command)
        self.assertEqual(command[-1], "--base.eval_episodes=8")

    def test_normal_profiles_still_use_normal_binary(self):
        command = self.command("terminal")
        self.assertEqual(Path(command[0]).name, "puffer")
        self.assertEqual(command[1], "train")

    def test_paired_default_and_explicit_noise_overrides(self):
        command = self.command("wld_paired")
        self.assertEqual(command[:2], ["KAG_CRITIC_MODE=2", "KAG_REWARD_WIN_LOSS_DRAW=1"])
        default = shlex.split(subprocess.run([sys.executable, str(SCRIPT), "train",
            "--dry-run"], check=True, capture_output=True, text=True).stdout)
        self.assertEqual(command, default)
        command = self.command("wld_paired", "train", "--env.opponent_noise_initial=0.25")
        self.assertIn("KAG_OPPONENT_NOISE_INITIAL=0.25", command)
        command = self.command("wld_paired", "match", "--env.opponent_noise_initial=1")
        self.assertIn("KAG_OPPONENT_NOISE_INITIAL=0", command)
        self.assertIn("KAG_OPPONENT_NOISE_FINAL=0", command)
        self.assertFalse(any(option.startswith("--env.opponent_noise_") for option in command))

    def test_main_config_is_paired_wld_with_six_rotating_opponent_slots(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(MAIN)
        for section, key, value in (("policy", "critic_mode", "2"),
                ("env", "reward_win_loss_draw", "1"), ("env", "reward_money", "1"),
                ("train", "gamma", "1"), ("train", "horizon", "720"),
                ("train", "minibatch_size", "11520"),
                ("train", "total_timesteps", "500000000"), ("train", "gpus", "1"),
                ("vec", "total_agents", "1024"), ("vec", "num_policies", "7"),
                ("vec", "hist_policy_percent", "0.5"), ("selfplay", "enabled", "1"),
                ("selfplay", "max_size", "100"),
                ("selfplay", "opp_timeout_steps", "10000000"),
                ("base", "checkpoint_interval", "4"),
                ("env", "opponent_noise_initial", "1"),
                ("env", "opponent_noise_decay_steps", "500000000"),
                ("selfplay", "initial_opponents", "None"), ("env", "num_agents", "2"),
                ("env", "num_bots", "0")):
            self.assertEqual(config[section][key], value, f"{section}.{key}")
        for key in ("reward_growth_land", "reward_growth_crop", "reward_growth_animal",
                "reward_alive_daily", "reward_quality_scale", "reward_quality_idle_cost",
                "potential_beta", "opponent_noise_final"):
            self.assertEqual(config.getfloat("env", key), 0, key)

    def test_profiles_preserve_main_config_bonus_and_reset_experiments(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(MAIN)
        for key in ("reward_growth_land", "reward_growth_crop", "reward_growth_animal"):
            config["env"][key] = "0.1"
        config["env"]["reset_state_prob"] = "0.5"
        config.read([SCRIPT.with_name("profiles") / "wld.ini",
            SCRIPT.with_name("profiles") / "wld_paired.ini"])
        self.assertEqual(config["policy"]["critic_mode"], "2")
        self.assertEqual(config["env"]["reward_win_loss_draw"], "1")
        for key in ("reward_growth_land", "reward_growth_crop", "reward_growth_animal"):
            self.assertEqual(config["env"][key], "0.1")
            command = self.command("wld_paired", "train", f"--env.{key}=0.2")
            self.assertIn(f"--env.{key}=0.2", command)
        self.assertEqual(config["env"]["reset_state_prob"], "0.5")
        command = self.command("wld_paired", "train", "--env.reset_state_prob=0.5")
        self.assertIn("--env.reset_state_prob=0.5", command)

    def test_wld_is_not_a_cash_return_sweep_or_offline_profile(self):
        for mode in ("sweep", "bc", "critic"):
            result = subprocess.run([sys.executable, str(SCRIPT), mode,
                "--profile", "wld", "--dry-run"], capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
