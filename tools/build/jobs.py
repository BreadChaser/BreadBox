"""Keep nested core makes in the caller's jobserver; bound standalone invocation."""
import os
import re


def make_parallelism(env):
    flags = env.get('MAKEFLAGS', '')
    match = re.search(r'--jobserver-(?:auth|fds)=(\d+),(\d+)', flags)
    if match:
        descriptors = tuple(map(int, match.groups()))
        for descriptor in descriptors:
            os.fstat(descriptor)  # A broken jobserver is an error, not unbounded fallback.
        return [], descriptors
    if '--jobserver-auth=fifo:' in flags or env.get('MAKELEVEL', '0') != '0':
        return [], ()
    jobs = env.get('BB_BUILD_JOBS', str(max(1, min(3, (os.cpu_count() or 2) // 2))))
    if not re.fullmatch(r'[1-9][0-9]*', jobs):
        raise ValueError('BB_BUILD_JOBS must be a positive integer')
    return ['-j' + jobs], ()
