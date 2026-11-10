#!/usr/bin/env python3
"""Cross-check MiniGit's supported object payloads against Git, in a temp repo."""
import pathlib
import subprocess
import sys
import tempfile


def main():
    exe = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build/minigit').resolve()
    with tempfile.TemporaryDirectory(prefix='minigit-git-check-') as tmp:
        root = pathlib.Path(tmp)

        def run(args, data=None):
            return subprocess.check_output(args, cwd=root, input=data).decode().strip()

        run([str(exe), 'init'])
        samples = [b'', b'hello\n', b'\x00\xff\x80abc', b'a' * 55, b'a' * 56,
                   b'a' * 63, b'a' * 64, b'a' * 65, bytes(range(256)) * 3]
        for i, data in enumerate(samples):
            path = root / 'nested' / f'file-{i}.bin'
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(data)
            actual = run([str(exe), 'hash-object', str(path)])
            expected = run(['git', 'hash-object', '--stdin'], data)
            assert actual == expected, (i, actual, expected)
        run([str(exe), 'add', '.'])
        run([str(exe), 'commit', '-m', 'Compatibility check'])
        objects = root / '.minigit' / 'objects'
        counts = {}
        for fanout in objects.iterdir():
            for file in fanout.iterdir():
                oid = fanout.name + file.name
                header, payload = file.read_bytes().split(b'\0', 1)
                kind, size = header.decode().split(' ')
                assert len(payload) == int(size)
                expected = run(['git', 'hash-object', '-t', kind, '--stdin'], payload)
                assert oid == expected, (kind, oid, expected)
                counts[kind] = counts.get(kind, 0) + 1
        assert {'blob', 'tree', 'commit'} <= counts.keys()
        print(f'PASS: {len(samples)} blob vectors and {sum(counts.values())} stored objects match Git: {counts}')


if __name__ == '__main__':
    main()
