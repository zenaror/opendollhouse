#!/usr/bin/env python3
import sys
import subprocess
import datetime
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]

def run_tests():
    process = subprocess.Popen([
        'ctest',
        '--output-on-failure',
        '--no-color',
        '-E', 'integration_tests',
        '-L', 'factory_tests'
    ],
    cwd=PROJECT_ROOT,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True)

    stdout, stderr = process.communicate()
    
    # Save test results
    with open(PROJECT_ROOT / 'reports' / 'factory_tests.log', 'w') as f:
        f.write(stdout)
        if stderr:
            f.write("\n\nErrors:\n")
            f.write(stderr)

    return process.returncode == 0

# Execute test run
test_success = run_tests()

# Output results
current_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
print(f"\nTest execution completed at {current_time} with status: {test_success}")
if not test_success:
    print("\nSee reports/factory_tests.log for details")
else:
    print("\nAll tests passed successfully")
