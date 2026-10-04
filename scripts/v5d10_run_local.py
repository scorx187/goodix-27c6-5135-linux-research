#!/usr/bin/env python3

import ast
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

PROBE = pathlib.Path('/home/sam/Downloads/fdt_probe_5135_v5.py')
REPO = pathlib.Path('/home/sam/libfprint')
BUILD = pathlib.Path('/tmp/goodix5135-v5d6-sigfm-optin')
BINARY = BUILD / 'examples/goodix5135-sigfm-calibration'
EXPECTED_HEAD = '2631f371e57ff7cb353b1c9389d277064e08c74c'
STATE_DIR = pathlib.Path('/home/sam/.local/state/goodix5135')
LOG_FILE = STATE_DIR / 'last-v5d10.log'


class Tee:
    def __init__(self, *streams):
        self.streams = streams

    def write(self, data):
        for stream in self.streams:
            stream.write(data)
            stream.flush()
        return len(data)

    def flush(self):
        for stream in self.streams:
            stream.flush()


def fail(message, code=1):
    print(f'SETUP_FAIL={message}')
    raise SystemExit(code)


def run_streamed(argv, *, cwd=None, env=None, check=False):
    process = subprocess.Popen(
        argv,
        cwd=cwd,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    assert process.stdout is not None
    for line in process.stdout:
        print(line, end='')

    rc = process.wait()
    if check and rc != 0:
        raise subprocess.CalledProcessError(rc, argv)
    return rc


def recover_refs():
    tree = ast.parse(PROBE.read_text())
    vals = {}

    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        if not isinstance(node.targets[0], ast.Name):
            continue

        name = node.targets[0].id
        value = node.value

        if name in ('PSK_FILE', 'GOODIX_DAT'):
            if (
                isinstance(value, ast.Call)
                and len(value.args) == 1
                and isinstance(value.args[0], ast.Constant)
                and isinstance(value.args[0].value, str)
                and (
                    (isinstance(value.func, ast.Name) and value.func.id == 'Path')
                    or
                    (isinstance(value.func, ast.Attribute) and value.func.attr == 'Path')
                )
            ):
                vals[name] = value.args[0].value

        elif name == 'WINDOWS_FDT_UP_REGS':
            if (
                isinstance(value, ast.Call)
                and isinstance(value.func, ast.Attribute)
                and value.func.attr == 'fromhex'
                and len(value.args) == 1
                and isinstance(value.args[0], ast.Constant)
                and isinstance(value.args[0].value, str)
            ):
                vals[name] = bytes.fromhex(value.args[0].value)

    required = {'PSK_FILE', 'GOODIX_DAT', 'WINDOWS_FDT_UP_REGS'}
    if set(vals) != required:
        fail('PRIVATE_REFERENCE_RECOVERY')

    return vals


def resolve_private(raw):
    p = pathlib.Path(raw).expanduser()
    candidates = [p]

    if not p.is_absolute():
        candidates.extend([
            PROBE.parent / p,
            pathlib.Path('/home/sam/goodix-fp-dump') / p,
            pathlib.Path('/home/sam/goodix-private') / p,
            pathlib.Path('/home/sam') / p,
        ])

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()

    return None


def main():
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    os.chmod(STATE_DIR, 0o700)

    log = LOG_FILE.open('w', buffering=1)
    os.chmod(LOG_FILE, 0o600)
    sys.stdout = Tee(sys.__stdout__, log)
    sys.stderr = Tee(sys.__stderr__, log)

    print(f'LOCAL_SAFE_LOG={LOG_FILE}')
    print('LOCAL_SAFE_LOG_PRIVATE_VALUES=NO')

    if not PROBE.is_file():
        fail('V5_PROBE_NOT_FOUND')
    if not REPO.is_dir():
        fail('LIBFPRINT_REPO_NOT_FOUND')
    if not BUILD.is_dir():
        fail('BUILD_NOT_FOUND')

    head = subprocess.check_output(
        ['git', '-C', str(REPO), 'rev-parse', 'HEAD'],
        text=True,
    ).strip()
    if head != EXPECTED_HEAD:
        fail('UNEXPECTED_LIBFPRINT_HEAD')

    status = subprocess.check_output(
        ['git', '-C', str(REPO), 'status', '--porcelain'],
        text=True,
    )
    if status.strip():
        fail('LIBFPRINT_WORKTREE_NOT_CLEAN')

    vals = recover_refs()
    psk = resolve_private(vals['PSK_FILE'])
    dat = resolve_private(vals['GOODIX_DAT'])

    if psk is None:
        fail('PSK_NOT_FOUND')
    if dat is None:
        fail('GOODIX_DAT_NOT_FOUND')

    psk_text = psk.read_text().strip()
    if len(psk_text) != 64 or any(c not in '0123456789abcdefABCDEF' for c in psk_text):
        fail('PSK_FORMAT')

    raw = dat.read_bytes()
    if len(raw) != 13520:
        fail('GOODIX_DAT_SIZE')

    seed = raw[64:76]
    up = vals['WINDOWS_FDT_UP_REGS']

    if len(seed) != 12:
        fail('FDT_SEED_LENGTH')
    if len(up) != 12:
        fail('FDT_UP_LENGTH')
    if any(seed[i] != seed[i + 1] for i in range(0, 12, 2)):
        fail('FDT_SEED_STRUCTURE')

    runtime = pathlib.Path(os.environ.get('XDG_RUNTIME_DIR', f'/run/user/{os.getuid()}'))
    if not runtime.is_dir():
        fail('RUNTIME_DIR_NOT_FOUND')

    tmp = pathlib.Path(tempfile.mkdtemp(prefix='goodix5135-v5d10-', dir=runtime))
    os.chmod(tmp, 0o700)

    try:
        seed_file = tmp / 'seed.bin'
        up_file = tmp / 'up.bin'
        seed_file.write_bytes(seed)
        up_file.write_bytes(up)
        os.chmod(seed_file, 0o600)
        os.chmod(up_file, 0o600)

        print('PRIVATE_INPUTS=READY')
        print('PRIVATE_VALUES_PRINTED=NO')
        print('V5D10_BUILDING=YES')

        run_streamed(
            ['ninja', '-C', str(BUILD), 'examples/goodix5135-sigfm-calibration'],
            check=True,
        )

        if not BINARY.is_file():
            fail('CALIBRATION_BINARY_MISSING')

        env = os.environ.copy()
        env.update({
            'GOODIX5135_LIVE_TLS_TEST': 'ONE_SHOT_NATIVE_TLS',
            'GOODIX5135_FPIMAGE_TEST': 'TWO_CAPTURE_LIFECYCLE',
            'GOODIX5135_LIVE_TLS_PSK_FILE': str(psk),
            'GOODIX5135_LIVE_FDT_SEED_FILE': str(seed_file),
            'GOODIX5135_LIVE_FDT_UP_FILE': str(up_file),
        })

        print()
        print('================================================')
        print(' GOODIX 5135 - V5D10 SIGFM CALIBRATION')
        print('================================================')
        print('Enroll: RIGHT_INDEX x30')
        print('Then follow each LIVE_SIGFM_PLACE_FINGER prompt exactly.')
        print('Lift the finger whenever cleanup is requested.')
        print('No biometric score or feature count will be printed.')
        print('================================================')
        print()

        rc = run_streamed(
            ['meson', 'devenv', '-C', str(BUILD), str(BINARY)],
            cwd=REPO,
            env=env,
        )

        print()
        print(f'LOCAL_V5D10_EXIT={rc}')
        print('LOCAL_V5D10_RESULT=' + ('PASS' if rc == 0 else 'REVIEW'))
        print(f'LOCAL_SAFE_LOG={LOG_FILE}')
        return rc

    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        print('EPHEMERAL_FDT_CLEANUP=PASS')


if __name__ == '__main__':
    sys.exit(main())
