import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

@unittest.skipUnless(sys.platform=='darwin' and os.environ.get('ISAAC_SUBTITLE_SMOKE_BINARY'),'opt-in Mac bundle acceptance')
class SubtitlePackageRuntimeTests(unittest.TestCase):
    def test_missing_worker_fails_the_in_application_package_check(self):
        executable=Path(os.environ['ISAAC_SUBTITLE_SMOKE_BINARY']).resolve()
        with tempfile.TemporaryDirectory(prefix='subtitle-package-check-') as directory:
            app=Path(directory)/'copy with spaces/IsaacVideoEditor.app';app.parent.mkdir();shutil.copytree(executable.parents[2],app)
            for candidate in (app/'Contents/MacOS/subtitles/whisper-cli',app/'Contents/Resources/subtitles/whisper-cli'):
                if candidate.exists():candidate.unlink()
            env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',SDL_AUDIODRIVER='dummy',ISAAC_DISABLE_HARDWARE_ENCODING='1',ISAAC_REQUIRE_SUBTITLE_RUNTIME='1')
            result=subprocess.run([str(app/'Contents/MacOS/IsaacVideoEditor'),'--smoke-test',str(Path(__file__).resolve().parents[1]/'tests/fixtures/seek.mp4')],env=env,capture_output=True,timeout=45)
            self.assertNotEqual(result.returncode,0,'Package check silently skipped the missing subtitle worker')
            self.assertIn(b'subtitle',result.stderr.lower())

if __name__=='__main__':unittest.main()
