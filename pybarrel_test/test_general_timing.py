import pybarrel
import time

ARCHIVE_NAME = "arch.brl"
HINTS        = 0x0000000000000002

print(f"Barrel Core Engine Version : {pybarrel.__BRL_VERSION__}")
print(f"PyBarrel Python Wrapper    : {pybarrel.__PYBRL_VERSION__}")

print(f"Writing Barrel archive {ARCHIVE_NAME} with hints {HINTS}")

pybarrel.Archive.create(ARCHIVE_NAME, hints=HINTS, initial_capacity=256)
with pybarrel.Archive(ARCHIVE_NAME) as arch:
    start = time.perf_counter_ns()
    arch.write("player_stats", b"level=42;hp=100;mp=50")
    end = time.perf_counter_ns()

    print(f"Write 1 took {end - start} ns")

    start = time.perf_counter_ns()
    arch.write("config_json", b'{"vsync": true, "volume": 80}')
    end = time.perf_counter_ns()

    print(f"Write 2 took {end - start} ns")

    start = time.perf_counter_ns()
    stats_view = arch.read("player_stats")
    end = time.perf_counter_ns()
    
    print(f"Read 1 took {end - start} ns")

    print("Read Back Player Data:", bytes(stats_view).decode("utf-8"))

    start = time.perf_counter_ns()
    json_view = arch.read("config_json")
    end = time.perf_counter_ns()

    print(f"Read 2 took {end - start} ns")

    print("Read Back Json Data:", bytes(json_view).decode("utf-8"))

    start = time.perf_counter_ns()
    arch.sync()
    end = time.perf_counter_ns()

    print(f"Sync took {end - start} ns")

    start = time.perf_counter_ns()
    arch.delete("config_json")
    end = time.perf_counter_ns()

    print(f"Delete took {end - start} ns")
