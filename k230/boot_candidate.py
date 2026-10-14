"""Candidate boot file only. Never automatically copied over /sdcard/main.py."""
import sys
sys.path.insert(0,'/sdcard/project_version/identity_auth_v1')
from run_auth import run
run()
