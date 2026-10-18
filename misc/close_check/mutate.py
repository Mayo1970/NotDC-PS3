#!/usr/bin/env python3
# GOALS.md G15 item 1 (WSL): plants one bug at a time in a copy of the sources; run.sh must FAIL on each.
import os, shutil, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
TMP = '/tmp/closemut'
Z, M, K = 'ps3/ps3_main.cpp', 'ps3/ps3_menu.cpp', 'Makefile'
FILES = [Z, M, K, 'misc/close_check/run.sh']
SPAWN = '\t\tsysProcessExitSpawn2(self, NULL, NULL, NULL, 0, 1001, SYS_PROCESS_SPAWN_STACK_SIZE_1M);\n'

MUTANTS = [
    ('Close Game is an XMB exit', Z, 'ps3_exit_requested = 2;', 'ps3_exit_requested = 1;'),
    ('every exit starts the file again', Z, 'if (ps3_exit_requested == 2)', 'if (ps3_exit_requested)'),
    ('no respawn', Z, '\tif (ps3_exit_requested == 2)\n' + SPAWN, ''),
    ('the respawn before the log socket closes', Z,
     '\tif (ps3_log_socket >= 0)\n\t\tclose(ps3_log_socket);\n',
     '\tif (ps3_exit_requested == 2)\n' + SPAWN + '\tif (ps3_log_socket >= 0)\n\t\tclose(ps3_log_socket);\n'),
    ('argv[0] is never taken', Z, '? argv[0] : PS3_SELF_PATH;', '? PS3_SELF_PATH : PS3_SELF_PATH;'),
    ('the wrong EBOOT.BIN', Z, '"/dev_hdd0/game/NOTDC0000/USRDIR/EBOOT.BIN"', '"/dev_hdd0/game/NOTDC0000/EBOOT.BIN"'),
    ('the spawn takes the NPDRM file', Z, '"%.*sRELOAD.SELF"', '"%.*sEBOOT.BIN"'),
    ('the folder loses its slash', Z, "(int)(strrchr(run, '/') + 1 - run)", "(int)(strrchr(run, '/') - run)"),
    ('the boot line has no spawn file', Z, ' %s, Close Game starts %s\\n", run, self);', ' %s\\n", run);'),
    ('no RELOAD.SELF in the package', K, '\t$(MAKE_SELF) $< $(BUILD)/pkg/USRDIR/RELOAD.SELF\n', ''),
    ('RELOAD.SELF is NPDRM', K, '$(MAKE_SELF) $< $(BUILD)/pkg/USRDIR/RELOAD.SELF',
     '$(MAKE_SELF_NPDRM) $< $(BUILD)/pkg/USRDIR/RELOAD.SELF $(CONTENT_ID)'),
    ('the log line has no close flag', Z, '(close game %d)\\n", ps3_exit_requested == 2);', '\\n");'),
    ('START opens the question in the disc browser', M, '\t\t\tif (swap)\n\t\t\t{\n\t\t\t\tScreen r = browser_input(pressed, true);',
     '\t\t\tif (pressed & MB_START)\n\t\t\t\task_close = true;\n\t\t\tif (swap)\n\t\t\t{\n\t\t\t\tScreen r = browser_input(pressed, true);'),
    ('START does not ask', M, '\t\t\telse if (pressed & MB_START)\n\t\t\t\task_close = true;\n', ''),
    ('X does not close', M, 'close_game = (pressed & MB_CROSS) != 0;', 'close_game = false;'),
    ('O does not go back', M, '\t\t\t\tif (pressed & MB_CIRCLE)\n\t\t\t\t\task_close = false;\n', ''),
    ('the question lets the presses through', M, '\t\t\t\tpressed = 0;\n', ''),
    ('no question on the screen', M, '\t\t\t\tif (ask_close)\n\t\t\t\t\tclose_draw();\n\t\t\t\telse if (swap)', '\t\t\t\tif (swap)'),
    ('the request before the presets are saved', M, '\t\tps3_presets_save();\n\t\tfor_each_option',
     '\t\tif (close_game)\n\t\t\tps3_close_game();\n\t\tps3_presets_save();\n\t\tfor_each_option'),
    ('no request', M, '\t\tif (close_game)\n\t\t\tps3_close_game();\n', ''),
    ('no START in the footer', M, 'START Close game   SELECT Back to game', 'SELECT Back to game'),
    ('no keys under the question', M, 'draw_footer(NULL, "X Yes   O No");', 'draw_footer(NULL, NULL);'),
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
    for name, f, find, repl in MUTANTS:
        text = open(os.path.join(ROOT, f), encoding='utf-8', newline='').read().replace('\r\n', '\n')
        open(os.path.join(TMP, f), 'w', encoding='utf-8', newline='').write(text.replace(find, repl))
        r = subprocess.run(['sh', os.path.join(TMP, 'misc/close_check/run.sh')], capture_output=True, text=True,
                           env=dict(os.environ, CLOSE_SRC=TMP))
        shutil.copy(os.path.join(ROOT, f), os.path.join(TMP, f))
        out = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith('FAIL')]
        ok = r.returncode == 1
        caught += ok
        print(f"{'caught' if ok else 'MISSED'}: {name}: {(out[0] if out else '')[:110]}", flush=True)
    print(f'{caught} of {len(MUTANTS)} mutants fail run.sh')
    sys.exit(0 if caught == len(MUTANTS) else 1)

main()
