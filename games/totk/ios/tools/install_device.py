#!/usr/bin/env python3
"""Sign and update an existing iPad app with bounded, logged installation stages."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import plistlib
import signal
import subprocess
import sys
import time


class InstallFailure(RuntimeError):
    pass


def values(value):
    if isinstance(value, dict):
        for child in value.values():
            yield from values(child)
    elif isinstance(value, list):
        for child in value:
            yield from values(child)
    else:
        yield value


def find_device(document, device_id):
    for device in document.get('result', {}).get('devices', []):
        if device_id.lower() in [v.lower() for v in values(device) if isinstance(v, str)]:
            return device
    return None


def locked(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key.lower() in ('islocked', 'deviceislocked', 'locked') and child is True:
                return True
            if locked(child):
                return True
    elif isinstance(value, list):
        return any(locked(child) for child in value)
    return False


def run_step(label, command, timeout, log_dir, hint, heartbeat=15):
    """Stream child output to terminal and disk; enforce a separate wall-clock deadline."""
    stamp = datetime.now().strftime('%H:%M:%S')
    print(f'\n[{stamp}] {label} (timeout: {timeout:g}s)', flush=True)
    path = log_dir / (label.split(' ', 1)[0].replace('/', '-') + '.log')
    start = time.monotonic()
    with path.open('wb') as output, path.open('rb') as reader:
        try:
            process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        except OSError as error:
            raise InstallFailure(f'{label}: could not start command: {error}\n{hint}\nLog: {path}') from error

        def stream():
            chunk = reader.read()
            if chunk:
                sys.stdout.write(chunk.decode('utf-8', errors='replace'))
                sys.stdout.flush()

        next_heartbeat = heartbeat
        timed_out = False
        try:
            while process.poll() is None:
                stream()
                elapsed = time.monotonic() - start
                if elapsed >= timeout:
                    timed_out = True
                    os.killpg(process.pid, signal.SIGKILL)
                    break
                if elapsed >= next_heartbeat:
                    print(f'  Still working on {label}: {elapsed:.0f}s / {timeout:g}s', flush=True)
                    next_heartbeat += heartbeat
                time.sleep(min(0.2, max(0.01, timeout - elapsed)))
        except KeyboardInterrupt:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
        process.wait()
        stream()
    elapsed = time.monotonic() - start
    if timed_out or process.returncode:
        reason = f'timed out after {elapsed:.1f}s' if timed_out else f'command exited with code {process.returncode}'
        raise InstallFailure(f'{label}: {reason}.\n{hint}\nLog: {path}')
    print(f'  OK: {label} ({elapsed:.1f}s)', flush=True)
    return path.read_text(errors='replace')


def verify_installed_build(document, bundle_id, expected):
    for app in document.get('result', {}).get('apps', []):
        if app.get('bundleIdentifier') == bundle_id:
            found = str(app.get('bundleVersion', 'unknown'))
            if found != expected:
                raise InstallFailure(f'The iPad reports build {found}; expected build {expected}. '
                                     'The requested update is not confirmed. Stopped before launch.')
            return found
    raise InstallFailure(f'{bundle_id} was not returned by the installed-app check. Stopped before launch.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--bundle-id', required=True)
    parser.add_argument('--entitlements', type=Path, required=True)
    parser.add_argument('--identity', default=os.environ.get('IOS_SIGNING_IDENTITY'),
                        help='codesigning identity (or IOS_SIGNING_IDENTITY)')
    parser.add_argument('--device', default=os.environ.get('IOS_DEVICE_ID'),
                        help='target device identifier (or IOS_DEVICE_ID)')
    parser.add_argument('--check-device', action='store_true', help='only discover and contact the iPad; do not sign/install')
    parser.add_argument('--log-dir', type=Path)
    args = parser.parse_args()
    if not args.device:
        parser.error('--device or IOS_DEVICE_ID is required')
    if not args.check_device and not args.identity:
        parser.error('--identity or IOS_SIGNING_IDENTITY is required')
    logs = args.log_dir or args.app.resolve().parent / 'install-logs' / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    logs.mkdir(parents=True, exist_ok=False)
    print(f'Installation logs: {logs}', flush=True)
    step = lambda label, command, timeout, hint: run_step(label, command, timeout, logs, hint)
    connect_hint = ('Keep the iPad unlocked and connected by a data-capable USB cable. Accept Trust on the iPad. '
                    'If CoreDeviceService times out, open Xcode → Window → Devices and Simulators, '
                    'wait for the iPad to become available, then rerun this script from Mac Terminal.')
    try:
        listing = logs / 'devices.json'
        step('1-Discovery Searching for your iPad', ['xcrun', 'devicectl', '--timeout', '30', 'list', 'devices', '--json-output', str(listing)], 40, connect_hint)
        device = find_device(json.loads(listing.read_text()), args.device)
        if device is None:
            raise InstallFailure(f'Target iPad {args.device} was not in the device list.\n{connect_hint}\nDevice list: {listing}')
        name = device.get('deviceProperties', {}).get('name', args.device)
        print(f'  FOUND: {name} ({args.device}). Checking live connection next.', flush=True)
        state = logs / 'lock-state.json'
        step('2-Connection Contacting the iPad', ['xcrun', 'devicectl', '--timeout', '25', 'device', 'info', 'lockState', '--device', args.device, '--json-output', str(state)], 35, connect_hint)
        if locked(json.loads(state.read_text()).get('result', {})):
            raise InstallFailure('The iPad responded but is locked. Unlock it and rerun. Nothing has been signed or installed.')
        print('  CONNECTED: the iPad responded to a live request.', flush=True)
        if args.check_device:
            print('Device check complete. No app files were changed.', flush=True)
            return 0
        info = plistlib.loads((args.app / 'Info.plist').read_bytes())
        if info.get('CFBundleIdentifier') != args.bundle_id:
            raise InstallFailure('App bundle ID does not match the requested update. Stopped before signing.')
        version = str(info.get('CFBundleVersion', ''))
        if not version:
            raise InstallFailure('App is missing its build number. Stopped before signing.')
        print(f'  APP: {args.bundle_id}, build {version}, from {args.app.resolve()}', flush=True)
        entitlements = plistlib.loads(args.entitlements.read_bytes())
        if not entitlements.get('application-identifier', '').endswith('.' + args.bundle_id):
            raise InstallFailure('Signing entitlements belong to a different app. Stopped before signing.')
        identity_hint = ('Open Keychain Access and unlock your login keychain. Check that your Apple Development '
                         'certificate includes its private key. Run this installer in Mac Terminal; restricted sessions '
                         'may not be able to access the signing identity.')
        identities = step('3-Identity Checking signing certificate', ['security', 'find-identity', '-v', '-p', 'codesigning'], 15, identity_hint)
        if args.identity.lower() not in identities.lower():
            raise InstallFailure(f'Required signing identity {args.identity} is unavailable.\n{identity_hint}')
        step('4-Signing Signing the app', ['codesign', '--force', '--sign', args.identity, '--timestamp=none', '--entitlements', str(args.entitlements), str(args.app)], 120,
             'If macOS asks for keychain access, allow codesign to use your development key. ' + identity_hint)
        step('5-Verification Verifying the signature', ['codesign', '--verify', '--strict', str(args.app)], 60,
             'Signature verification failed. The app has not been installed; inspect the log before retrying.')
        step('6-Installation Installing the app on your iPad', ['xcrun', 'devicectl', '--timeout', '1200', 'device', 'install', 'app', '--device', args.device, str(args.app)], 1215,
             'The transfer can take around 10 minutes. Keep the iPad connected and unlocked, check free space, '
             'and enable Developer Mode if requested. ' + connect_hint)
        installed = logs / 'installed-apps.json'
        step('7-InstalledBuild Checking the installed build', ['xcrun', 'devicectl', '--timeout', '30', 'device', 'info', 'apps', '--device', args.device, '--json-output', str(installed)], 40,
             'Installation returned success, but its build could not be verified. ' + connect_hint)
        verify_installed_build(json.loads(installed.read_text()), args.bundle_id, version)
        print(f'  INSTALLED: verified build {version} on the iPad. Opening it next.', flush=True)
        step('8-Launch Opening the updated app', ['xcrun', 'devicectl', '--timeout', '30', 'device', 'process', 'launch', '--device', args.device, '--terminate-existing', args.bundle_id], 40,
             'The update was installed, but launching failed. Unlock your iPad and open the app manually. '
             'Check Settings → Privacy & Security → Developer Mode if needed.')
        print(f'\nDONE: build {version} verified on the iPad and opened.', flush=True)
        return 0
    except (InstallFailure, OSError, ValueError, plistlib.InvalidFileException) as error:
        print(f'\nSTOPPED: {error}\nLogs: {logs}', file=sys.stderr, flush=True)
        return 1
    except KeyboardInterrupt:
        print(f'\nCancelled. No later stages will run. Logs: {logs}', file=sys.stderr)
        return 130


if __name__ == '__main__':
    sys.exit(main())
