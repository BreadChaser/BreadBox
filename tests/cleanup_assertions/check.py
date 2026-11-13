"""Exercise the actual replay assertion guard against a synthetic cleanup history."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
script=root/'tools/cleanup/replay.sh'
with tempfile.TemporaryDirectory(prefix='cleanup-assertions-') as tmp:
    repo=Path(tmp)
    def git(*args):return subprocess.run(['git',*args],cwd=repo,check=True,capture_output=True,text=True)
    git('init','-q');git('config','user.name','Host test');git('config','user.email','host@example.invalid')
    path=repo/'tests/main_lifecycle/check.py';path.parent.mkdir(parents=True)
    path.write_text("assert normal.count('exit_terminal();') == 2 and 'exit_keep_heap' not in s\nassert ready\nassert ready\n")
    git('add','.');git('commit','-qm','Assertion reference');ref=git('rev-parse','HEAD').stdout.strip()
    def check(ok):
        result=subprocess.run(['bash',str(script),'--check-assertions',ref],cwd=repo,capture_output=True,text=True)
        assert (result.returncode==0)==ok,(result.stdout,result.stderr)
        if not ok:assert 'assertion preservation FAILED' in result.stderr
    check(True)
    # Reformatting and splitting a conjunction retain the assertions.
    good="assert normal.count( 'exit_terminal();' ) == 2\nassert 'exit_keep_heap' not in s\nassert ready\nassert ready\n"
    path.write_text(good);check(True)
    path.write_text(good.replace("assert normal.count( 'exit_terminal();' ) == 2\n",''));check(False)
    path.write_text(good.replace('== 2','== 1'));check(False)
    path.write_text(good.replace('assert ready\n','',1));check(False)
    path.unlink();check(False)
print('Cleanup assertion guard PASS: unchanged/reformatted tests accepted; dropped, weakened, duplicate and deleted assertions rejected')
