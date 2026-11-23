#!/usr/bin/env python3
# fuzz_mate_dump.cpp の出力を YaneuraOu に食べさせ、詰み判定を照合する。
# M = 当アプリが詰みと判定 -> エンジンは resign するはず
# N = 当アプリが非詰みと判定 -> エンジンは mate 付き resign を出してはいけない
import random
import subprocess
import sys

eng = subprocess.Popen(
    ['engines/yaneuraou_x86_64'], cwd='.',
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

def send(s):
    eng.stdin.write(s + '\n')
    eng.stdin.flush()

def read_until(pred):
    now_mated = False
    while True:
        line = eng.stdout.readline()
        if not line:
            return None, now_mated
        if pred(line):
            return line, now_mated
        # YaneuraOuは詰み状態だと depth 0 の時点で score mate -N + pv resign を出す
        if line.startswith('info depth 0 ') and 'score mate -' in line:
            now_mated = True

send('usi')
send('setoption name Threads value 1')
send('setoption name Hash value 32')
send('isready')
read_until(lambda l: l.startswith('readyok'))

lines = [l.strip() for l in open(sys.argv[1]) if l.strip()]
random.seed(1)
mates = [l for l in lines if l.endswith(' M')]
nots = [l for l in lines if l.endswith(' N')]
random.shuffle(nots)
sample = mates + nots[:1200]
print(f'checking {len(sample)} positions ({len(mates)} claimed mates)')

bugs = 0
for l in sample:
    parts = l.split()
    tag = parts[-1]
    sfen = ' '.join(parts[:-1])
    send('position sfen ' + sfen)
    send('go btime 80 wtime 80')
    line, now_mated = read_until(lambda l: l.startswith('bestmove'))
    if line is None:
        print('ENGINE DIED at:', l); break
    mv = line.split()[1]
    # M線: 実指し手が続くなら「詰みと誤判定」。resign(理由を問わず)は整合的。
    if tag == 'M' and mv != 'resign':
        print(f'OVERCLAIM(当アプリが詰みと誤判定?) {sfen} -> {mv}')
        bugs += 1
    # N線: depth 0で mate -N を出す=「まさに詰み」の状態のみ見落としとして数える
    # (将来自玉になる局面の resign/mate は見落としではない)
    if tag == 'N' and now_mated:
        print(f'MISSED(当アプリが詰みを見落とし) {sfen}')
        bugs += 1
send('quit')
print('FUZZ BUGS', bugs)
sys.exit(1 if bugs else 0)
