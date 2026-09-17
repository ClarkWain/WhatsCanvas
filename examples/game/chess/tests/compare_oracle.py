"""Optional differential rule check against python-chess (test dependency only).

Install python-chess==1.999 separately; it is not linked or shipped with the game.
Run: python compare_oracle.py --engine ../build/Release/ChessTests.exe
"""
import argparse
import json
import random
import subprocess
import chess


def positions():
    # Special positions supplement random playouts, which rarely visit promotions.
    for fen in (
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "4k3/P7/8/8/8/8/8/4K3 w - - 0 1",
        "1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1",
        "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1",
        "7k/6Q1/5K2/8/8/8/8/8 b - - 0 1",
    ):
        board = chess.Board(fen)
        yield board.copy()
        # Cover both colors and mirrored castling/promotion/en-passant geometry.
        yield board.mirror()
    randomizer = random.Random(9132026)
    for _ in range(12):
        board = chess.Board()
        for _ in range(90):
            yield board.copy()
            moves = list(board.legal_moves)
            if not moves:
                break
            board.push(randomizer.choice(moves))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True)
    args = parser.parse_args()
    checked = children = 0
    with subprocess.Popen([args.engine, "--oracle"], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True, encoding="utf-8") as engine:
        try:
            for board in positions():
                fen = board.fen(en_passant="fen")
                engine.stdin.write(fen + "\n")
                engine.stdin.flush()
                result = json.loads(engine.stdout.readline())
                actual = dict(result["moves"])
                expected = {move.uci(): move for move in board.legal_moves}
                assert actual.keys() == expected.keys(), (fen, sorted(actual.keys() ^ expected.keys()))
                assert result["check"] == board.is_check(), fen
                assert result["material"] == board.is_insufficient_material(), fen
                for uci, move in expected.items():
                    child = board.copy()
                    child.push(move)
                    assert actual[uci] == child.fen(en_passant="fen"), (fen, uci, actual[uci], child.fen(en_passant="fen"))
                    children += 1
                checked += 1
        finally:
            engine.stdin.close()
        assert engine.wait(timeout=10) == 0
    print(f"PASS oracle positions={checked} child_positions={children} python-chess={chess.__version__}")


if __name__ == "__main__":
    main()
