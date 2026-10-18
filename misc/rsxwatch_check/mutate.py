#!/usr/bin/env python3
# RSX stall log (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/rsxwmut'
W, H, V, K = 'ps3/ps3_rsxwatch.c', 'ps3/ps3_rsxwatch.h', 'ps3/ps3_video.c', 'Makefile'
FILES = [W, H, V, K, 'misc/rsxwatch_check/rsxwatch_check.c', 'misc/rsxwatch_check/run.sh',
         'misc/rsxwatch_check/fake/rsx/gcm_sys.h', 'misc/rsxwatch_check/fake/ps3_timebase.h']

MUTANTS = [
    ('the first dump at once', W, 'dt >= 1.0', 'dt >= 0.0'),
    ('no second dump', W, '(s_rw.dumps == 1 && dt >= 5.0)', '(0)'),
    ('a dump at each skip after 5 s', W, 's_rw.dumps == 1 && dt >= 5.0', 's_rw.dumps >= 1 && dt >= 5.0'),
    ('the streak starts at each skip', W, '\tif (!s_rw.skip_tb)\n\t\ts_rw.skip_tb = gettime();\n', '\ts_rw.skip_tb = gettime();\n'),
    ('a flip does not end the streak', W, '\ts_rw.skip_tb = 0;\n', ''),
    ('a flip keeps the dump count', W, '\ts_rw.skips = s_rw.dumps = 0;\n', '\ts_rw.skips = 0;\n'),
    ('the back line with no stall', W, '\tif (s_rw.dumps)\n\t\tprintf("[rsxw] flips back', '\tif (1)\n\t\tprintf("[rsxw] flips back'),
    ('the skips are not counted', W, '\ts_rw.skips++;\n', ''),
    ('the ring keeps the first frames', W, 's_rw.ring[s_rw.head++ % RW_FRAMES]', 's_rw.ring[s_rw.head < RW_FRAMES ? s_rw.head++ : RW_FRAMES - 1]'),
    ('one frame too few', W, 'for (u32 i = s_rw.head - have; i != s_rw.head; i++)', 'for (u32 i = s_rw.head - have + 1; i != s_rw.head; i++)'),
    ('the next frame keeps the old counts', W, '\tmemset(&s_rw.next, 0, sizeof(s_rw.next));\n', ''),
    ('the binds in the vertex column', W, 'f->verts = ps3gl_frame_flush_vertex_count;', 'f->verts = ps3gl_frame_tex_binds;'),
    ('no scene size', W, '\tf->ns_h = ns_h;\n', ''),
    ('the frame keeps get, not put', W, 'f->put = gcmGetControlRegister()->put;', 'f->put = gcmGetControlRegister()->get;'),
    ('no words before get', W, 'from = w - RW_WORDS < b ? b : w - RW_WORDS;', 'from = w;'),
    ('words before the buffer start', W, 'from = w - RW_WORDS < b ? b : w - RW_WORDS;', 'from = w - RW_WORDS;'),
    ('words past the buffer end', W, '\t\t\tto = to > e ? e : to;\n', ''),
    ('the bracket on the first word', W, 'p == w ? " [%08X]"', 'p == from ? " [%08X]"'),
    ('get and put swapped', W, 'const u32 put = c->put, get = c->get, ref = c->ref;', 'const u32 put = c->get, get = c->put, ref = c->ref;'),
    ('no line for a get with no address', W, '\telse\n\t\tprintf("[rsxw] get %08X is no IO address\\n", (unsigned)get);\n', ''),
    ('no skip hook', V, '\t\tps3_rsxwatch_skip(s_ctx, s_flip_queued, s_flip_done);\n', ''),
    ('the flip hook before the flush', V, '\trsxFlushBuffer(s_ctx);\n\tps3_rsxwatch_flip(s_flip_queued);\n',
     '\tps3_rsxwatch_flip(s_flip_queued);\n\trsxFlushBuffer(s_ctx);\n'),
    ('a held frame has no counts', V, '\tps3_rsxwatch_frame(s_ns.scene_w, s_ns.scene_h);\n\tps3gl_end_frame();\n\trsxFlushBuffer(s_ctx);\n\ts_frame_waits = 1;',
     '\tps3gl_end_frame();\n\trsxFlushBuffer(s_ctx);\n\ts_frame_waits = 1;'),
    ('the counts after ps3gl cleared them', V, '\tps3_rsxwatch_frame(s_ns.scene_w, s_ns.scene_h);\n\tps3gl_end_frame();\n\tqueue_flip(0);',
     '\tps3gl_end_frame();\n\tps3_rsxwatch_frame(s_ns.scene_w, s_ns.scene_h);\n\tqueue_flip(0);'),
    ('the log in the main TOC', K, '$(BUILD)/ps3/ps3_rsxwatch.o: CFLAGS += -mminimal-toc\n', ''),
]

def main():
    shutil.rmtree(TMP, ignore_errors=True)
    for f in FILES:
        os.makedirs(os.path.dirname(os.path.join(TMP, f)), exist_ok=True)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
    bad = 0
    for name, f, find, repl in MUTANTS:
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        if text.count(find) != 1:
            print(f'BAD MUTANT (pattern found {text.count(find)} times): {name}')
            bad += 1
    if bad:
        sys.exit(2)
    caught = 0
    env = dict(os.environ, RW_SRC=TMP, RW_TMP='/tmp/rsxwmut_build')
    for name, f, find, repl in MUTANTS:
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/rsxwatch_check/run.sh')], capture_output=True, text=True, env=env)
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith(('FAIL', 'ERROR')) or 'error' in l]
        if r.returncode == 2:
            print(f'NO BUILD: {name}: {(out[0] if out else "")[:110]}', flush=True)
            continue
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
