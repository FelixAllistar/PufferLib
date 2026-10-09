'use strict';
const { spawn } = require('node:child_process');
const readline = require('node:readline');

// One bounded in-flight request to a local line-oriented native process.
function lineRpc(program, args = []) {
  const child = spawn(program, args, { stdio: ['pipe', 'pipe', 'inherit'] });
  const lines = readline.createInterface({ input: child.stdout });
  let pending, dead = false;
  const fail = error => {
    dead = true;
    if (pending) { clearTimeout(pending.timer); pending.reject(error); pending = null; }
  };
  lines.on('line', line => {
    if (!pending) return;
    const { resolve, reject, timer } = pending; pending = null; clearTimeout(timer);
    try { const value = JSON.parse(line); if (value.error) reject(Error(value.error)); else resolve(value); }
    catch (error) { reject(error); }
  });
  child.on('error', fail); child.stdin.on('error', fail);
  child.on('exit', (code, signal) => fail(Error(`${program} exited ${code ?? signal}`)));
  child.on('close', () => { dead = true; });
  return {
    get dead() { return dead; },
    request(value) { return new Promise((resolve, reject) => {
      if (dead || pending || child.exitCode !== null || child.signalCode !== null) return reject(Error(`${program} unavailable`));
      const timer = setTimeout(() => { fail(Error(`${program} timeout`)); child.kill('SIGTERM'); }, 15000);
      pending = { resolve, reject, timer };
      child.stdin.write(JSON.stringify(value) + '\n', error => { if (error) fail(error); });
    }); },
    close() {
      fail(Error(`${program} closed`));child.stdin.end();lines.close();child.kill('SIGTERM');
    },
  };
}
module.exports = { lineRpc };
