"""Host-only lifecycle harness; substitutes paths, never runs the device binary."""
import pathlib, subprocess, tempfile, os, signal
source = pathlib.Path(__file__).resolve().parents[1] / 'rootfs/etc/init.d/K95screw-guard'
with tempfile.TemporaryDirectory(prefix='screw-guard-init-') as tmp:
    root = pathlib.Path(tmp)
    c = root / 'stub.c'
    c.write_text('''#include <signal.h>\n#include <unistd.h>\n#include <stdio.h>\nint main(int argc,char**argv){signal(SIGUSR1,SIG_DFL);FILE*f=fopen(argv[2],"r");if(!f)return 2;if(fgetc(f)=='i')signal(SIGTERM,SIG_IGN);fclose(f);for(;;)pause();}\n''')
    app = root / 'stub'
    subprocess.run(['cc', str(c), '-o', str(app)], check=True)
    config, pidfile = root / 'config', root / 'pid'
    script = root / 'init'
    script.write_text(source.read_text().replace('/usr/local/bin/screw-guard',str(app)).replace('/userdata/local/apps/screw-guard.config.json',str(config)).replace('/var/run/screw-guard.pid',str(pidfile)).replace('/var/log/screw-guard.log',str(root/'log')))
    def run(action, expected):
        p = subprocess.run(['sh', str(script), action], text=True, capture_output=True, timeout=16)
        print(action, 'rc=',p.returncode, p.stdout.strip(), p.stderr.strip(), flush=True)
        assert p.returncode == expected
    run('start',1)
    config.write_text('normal')
    run('start',0)
    pid = int(pidfile.read_text())
    try:
        run('start',0)
        assert int(pidfile.read_text()) == pid
        pidfile.unlink()
        run('start',1)
        run('stop',1)
        pidfile.write_text(str(pid))
        run('stop',0)
        assert not pidfile.exists()
        pidfile.write_text(str(os.getpid()))
        run('stop',1)
        run('start',1)
        pidfile.unlink()
        config.write_text('ignore')
        run('start',0)
        pid = int(pidfile.read_text())
        run('stop',1)
        assert pidfile.exists()
        os.kill(pid,signal.SIGUSR1)
        print('PASS: missing config, idempotency, lost PID, identity mismatch, graceful stop and bounded timeout')
    finally:
        try: os.kill(pid,signal.SIGUSR1)
        except ProcessLookupError: pass
