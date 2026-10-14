"""Run all pure software tests and save reproducible counts and transcripts."""
import io, json, sys, time, unittest
from pathlib import Path
root=Path(__file__).resolve().parent
sys.path.insert(0,str(root))
suite=unittest.defaultTestLoader.discover(str(root/'tests'))
stream=io.StringIO()
start=time.monotonic()
result=unittest.TextTestRunner(stream=stream,verbosity=2).run(suite)
elapsed=time.monotonic()-start
folder=root/'evidence'
folder.mkdir(exist_ok=True)
(folder/'software_tests.log').write_text(stream.getvalue(),encoding='utf-8')
summary={'type':'pure_software_CPython','python':sys.version,'tests':result.testsRun,
         'failures':len(result.failures),'errors':len(result.errors),
         'skipped':len(result.skipped),'elapsed_seconds':elapsed,
         'success':result.wasSuccessful()}
(folder/'software_tests.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(stream.getvalue())
print(json.dumps(summary,indent=2))
sys.exit(0 if result.wasSuccessful() else 1)
