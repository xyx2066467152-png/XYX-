"""Build and execute the actual Cortex-M3 C parser/policy; no serial or hardware.

Run with Python 3.12 and existing Unicorn/pyelftools. No packages are downloaded.
Python uart_protocol.py is loaded as the independent framing/payload oracle.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import random
import struct
import subprocess
import sys
import time

sys.dont_write_bytecode = True
TESTS = Path(__file__).resolve().parent
ROOT = TESTS.parent
DEFAULT_DEPS = TESTS / ".deps"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--project-root", type=Path, default=ROOT)
parser.add_argument("--output-root", type=Path, default=TESTS)
parser.add_argument("--python-protocol", type=Path)
parser.add_argument("--deps", type=Path, default=Path(os.getenv("K230_TEST_DEPS", str(DEFAULT_DEPS))))
parser.add_argument("--compiler", type=Path, default=Path("D:/Keil_v5/ARM/ARMCC/bin/armcc.exe"))
parser.add_argument("--no-build", action="store_true")
args = parser.parse_args()
ROOT = args.project_root.resolve()
if args.python_protocol is None:
    args.python_protocol = ROOT.parents[1] / "k230/uart_protocol.py"
args.output_root.mkdir(parents=True, exist_ok=True)
BUILD = args.output_root / ".build"
BUILD.mkdir(exist_ok=True)
sys.path.insert(0, str(args.deps))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                              UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR,
                              UC_ARM_REG_XPSR)
from elftools.elf.elffile import ELFFile

spec = importlib.util.spec_from_file_location("python_protocol_oracle", args.python_protocol)
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)
ELF = BUILD / "protocol_tests.axf"
SOURCES = [ROOT / "Drivers/Src/k230_protocol.c", ROOT / "Services/Src/face_auth.c",
           TESTS / "protocol_bridge.c"]

def build():
    log = []
    objects = []
    for source in SOURCES:
        obj = BUILD / (source.stem + ".o")
        if args.compiler.name.lower() == "armcc.exe":
            command = [str(args.compiler), "--cpu=Cortex-M3", "--c99", "-O1", "-g"]
        else:
            command = [str(args.compiler), "--target=arm-arm-none-eabi", "-mcpu=cortex-m3",
                       "-mthumb", "-std=c99", "-O1", "-g", "-ffreestanding", "-fno-builtin",
                       "-Wall", "-Wextra", "-Werror"]
        for folder in ("Drivers/Inc", "Services/Inc", "App/Inc", "Config"):
            command += ["-I", str(ROOT / folder)]
        command += ["-c", str(source), "-o", str(obj)]
        result = subprocess.run(command, capture_output=True, text=True)
        log.append(subprocess.list2cmdline(command) + "\n" + result.stdout + result.stderr)
        if result.returncode:
            (BUILD / "build.log").write_text("\n".join(log), encoding="utf-8")
            raise RuntimeError("C compile failed:\n" + result.stdout + result.stderr)
        objects.append(str(obj))
    command = [str(args.compiler.parent / "armlink.exe"), "--no_scanlib", "--no_remove",
               "--entry=Test_Init", "--ro_base=0x08000000", "--rw_base=0x20000000",
               "--map", "--list=" + str(BUILD / "protocol_tests.map"),
               "--output=" + str(ELF)] + objects
    result = subprocess.run(command, capture_output=True, text=True)
    log.append(subprocess.list2cmdline(command) + "\n" + result.stdout + result.stderr)
    (BUILD / "build.log").write_text("\n".join(log), encoding="utf-8")
    if result.returncode:
        raise RuntimeError("C link failed:\n" + result.stdout + result.stderr)

if not args.no_build:
    build()

machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
machine.mem_map(0x08000000, 0x40000)
machine.mem_map(0x20000000, 0x40000)
with ELF.open("rb") as handle:
    image = ELFFile(handle)
    # ARMCC scatter loading can put RAM execution sections in one flash PT_LOAD.
    # Load allocated sections at execution addresses instead of running startup.
    for section in image.iter_sections():
        if section["sh_flags"] & 2 and section["sh_type"] != "SHT_NOBITS" and section["sh_size"]:
            machine.mem_write(section["sh_addr"], section.data())
    symbols = {symbol.name: symbol["st_value"]
               for symbol in image.get_section_by_name(".symtab").iter_symbols()
               if symbol.name}
STOP, STACK, SCRATCH = 0x0803FFF0, 0x2003F000, 0x20020000
returned = False

def stop(uc, address, size, context):
    global returned
    returned = True
    uc.emu_stop()

machine.hook_add(UC_HOOK_CODE, stop, begin=STOP, end=STOP)

def call(name, *values):
    global returned
    returned = False
    for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), values):
        machine.reg_write(reg, value & 0xFFFFFFFF)
    if len(values) > 4:
        machine.mem_write(STACK, struct.pack("<" + "I" * (len(values)-4), *values[4:]))
    machine.reg_write(UC_ARM_REG_SP, STACK)
    machine.reg_write(UC_ARM_REG_LR, STOP | 1)
    machine.reg_write(UC_ARM_REG_XPSR, 0x01000000)
    machine.emu_start(symbols[name] | 1, STOP + 2, count=50000000)
    assert returned, name + " did not return within instruction budget"
    return machine.reg_read(UC_ARM_REG_R0)

def put(data):
    assert len(data) < 0x1E000
    machine.mem_write(SCRATCH, bytes(data) or b"\0")
    return SCRATCH

def captured():
    count = struct.unpack("<I", machine.mem_read(symbols["test_frame_count"], 4))[0]
    assert count <= 256, "test callback capture capacity exceeded"
    result = []
    raw = bytes(machine.mem_read(symbols["test_frames"], count * 37))
    for offset in range(0, len(raw), 37):
        row = raw[offset:offset+37]
        result.append({"version": row[0], "cmd": row[1],
                       "seq": row[2] | row[3] << 8,
                       "data": row[5:5+row[4]], "known": row[1] in oracle.COMMAND_NAMES})
    return result

STAT_NAMES = tuple(oracle.FrameParser().stats)
records = []
started = time.monotonic()

def check(label, function):
    try:
        function()
        records.append({"name": label, "passed": True})
    except Exception as exc:
        records.append({"name": label, "passed": False, "error": repr(exc)})
        print("FAIL:", label, repr(exc), flush=True)

def stream_case(actions):
    call("Test_Init")
    reference = oracle.FrameParser()
    expected = []
    for kind, data, now in actions:
        if kind == "feed":
            actual_count = call("Test_Feed", put(data), len(data), now)
            frames = reference.feed(data, now)
        elif kind == "byte":
            assert len(data) == 1
            actual_count = call("Test_FeedByte", data[0], now)
            frames = reference.feed(data, now)
        elif kind == "expire":
            actual_count = call("Test_Expire", now)
            frames = reference.expire(now)
        elif kind == "reset":
            call("Test_Reset")
            reference.reset()
            actual_count, frames = 0, []
        else:
            raise AssertionError(kind)
        assert actual_count == len(frames), (kind, actual_count, len(frames))
        expected.extend(frames)
        assert captured() == expected, "callback frames differ from Python"
        actual_stats = {key: call("Test_Stat", index) for index, key in enumerate(STAT_NAMES)}
        assert actual_stats == reference.stats, (actual_stats, reference.stats)
        assert call("Test_Stat", 8) == reference.buffered_bytes
        assert call("Test_Stat", 7) <= 41
        assert call("Test_Guards") == 1, "parser overwrote guard bytes"

def message(cmd, seq=0, **fields):
    return oracle.encode_frame(cmd, oracle.pack_message(cmd, **fields), seq)

golden = [
    message(oracle.CMD_MODULE_READY, 0, api_major=1, api_minor=2,
            feature_dimension=128, threshold=0.7),
    message(oracle.CMD_AUTH_SUCCESS, 0x1234, user_id=1, similarity=0.875),
    message(oracle.CMD_UNKNOWN, 2, reason=1), message(oracle.CMD_NO_FACE, 3),
    message(oracle.CMD_ERROR, 4, error_code=0x10, detail=0x1234),
    message(oracle.CMD_PING, 0xFFFF), message(oracle.CMD_GET_STATUS, 6),
    message(oracle.CMD_PONG, 0xFFFF),
    message(oracle.CMD_STATUS, 8, state=2, last_event=3, last_user_id=1,
            last_similarity=0.99, last_error=0),
    oracle.encode_frame(0xFE, bytes(range(32)), 0x55AA),
    oracle.encode_frame(0xFA, b"\xaa\x55", 9),
]

def assert_equal(actual, expected):
    assert actual == expected, (actual, expected)

# Known CRC check, plus independent Python vectors for C encoding.
check("CRC CCITT-FALSE 123456789 = 29B1", lambda: assert_equal(
    call("K230_CRC16", put(b"123456789"), 9), 0x29B1))
check("CRC empty = FFFF", lambda: assert_equal(call("K230_CRC16", 0, 0), 0xFFFF))

def encoding(frame):
    size = call("Test_Encode", frame[3], int.from_bytes(frame[4:6], "little"),
                put(frame[7:-2]), frame[6], 64)
    assert size == len(frame)
    assert bytes(machine.mem_read(symbols["test_encoded"], size)) == frame
    assert bytes(machine.mem_read(symbols["test_encoded"] + size, 64-size)) == b"\xA5"*(64-size)

for index, frame in enumerate(golden):
    check("Python golden encode %02d cmd %02X" % (index, frame[3]), lambda f=frame: encoding(f))
    check("normal frame %02d" % index, lambda f=frame: stream_case([("feed", f, 0)]))
    for cut in range(len(frame)+1):
        check("every split %02d at %02d" % (index, cut), lambda f=frame, c=cut:
              stream_case([("feed", f[:c], 0), ("feed", f[c:], 1)]))
    check("bytewise FeedByte %02d" % index, lambda f=frame: stream_case(
          [("byte", bytes([value]), tick) for tick, value in enumerate(f)]))

joined = b"".join(golden)
check("all golden frames concatenated callback preserves every frame", lambda: stream_case([("feed", joined, 0)]))
for cut in range(len(joined)+1):
    check("concatenated split %03d" % cut, lambda c=cut: stream_case(
        [("feed", joined[:c], 0), ("feed", joined[c:], 1)]))

for length in range(33):
    frame = oracle.encode_frame(0xF0, bytes(range(length)), length)
    check("payload wire length %02d" % length, lambda f=frame: (encoding(f), stream_case([("feed", f, 0)])))

good = golden[1]
for length in range(33, 256):
    invalid = b"\xAA\x55\x01\x10\x00\x00" + bytes([length])
    check("invalid wire LEN %03d recovery" % length, lambda b=invalid:
          stream_case([("feed", b+good, 0)]))

for version in (0, 2, 0x7F, 0xFF):
    invalid = bytearray(good); invalid[2] = version
    check("unsupported version %02X recovery" % version, lambda b=bytes(invalid): stream_case([("feed", b+good, 0)]))
for byte_index in range(2, len(good)):
    for bit in range(8):
        invalid = bytearray(good); invalid[byte_index] ^= 1 << bit
        check("corruption byte %02d bit %d resync" % (byte_index, bit), lambda b=bytes(invalid):
              stream_case([("feed", b+good, 0), ("expire", b"", 200)]))

inner = message(oracle.CMD_NO_FACE, 0xAA55)
outer = oracle.encode_frame(0xEE, b"\xaa\x55"+inner+b"\xaa", 0x55AA)
check("AA55 and complete valid frame inside DATA are not split", lambda: stream_case([("feed", outer+good, 0)]))
for cut in range(1, len(good)):
    check("truncated frame %02d expiry and recovery" % cut, lambda c=cut: stream_case(
        [("feed", good[:c], 0), ("expire", b"", 199), ("expire", b"", 200), ("feed", good, 201)]))
bogus = b"\xaa\x55\x01\xFE\x00\x00\x20" + good
check("truncated LEN32 hides good frame until 200ms", lambda: stream_case(
    [("feed", bogus, 0), ("expire", b"", 199), ("expire", b"", 200)]))
check("slow bytes cannot extend first-byte deadline", lambda: stream_case(
    [("feed", good[:1], 0), ("feed", good[1:4], 190), ("expire", b"", 199),
     ("expire", b"", 200), ("feed", good, 201)]))
check("uint32 clock wraps across exact 200ms deadline", lambda: stream_case(
    [("feed", bogus, 0xFFFFFFF0), ("expire", b"", 0xFFFFFFF0+199),
     ("expire", b"", 0xFFFFFFF0+200)]))
check("ParserReset preserves cumulative stats and callback", lambda: stream_case(
    [("feed", good, 0), ("feed", good[:4], 1), ("reset", b"", 2), ("feed", good, 3)]))

rng = random.Random(230)
noise = bytes(rng.randrange(256) for _ in range(24000)) + b"\xaa"*4096
check("28096 noise bytes storage <=41 and guards intact", lambda: stream_case(
    [("feed", noise, 0), ("expire", b"", 200), ("feed", joined, 201)]))
for trial in range(30):
    stream = b"".join((bytes(rng.randrange(256) for _ in range(rng.randrange(20))) +
                       rng.choice(golden)) for _ in range(12))
    cuts, index = [], 0
    while index < len(stream):
        width = rng.randrange(1, 45)
        cuts.append(("feed", stream[index:index+width], 5)); index += width
    cuts.append(("expire", b"", 205))
    check("deterministic noisy chunk stream %02d" % trial, lambda acts=cuts: stream_case(acts))

def packed(cmd, data=b"", seq=0, version=1, declared=None):
    return bytes([version, cmd])+struct.pack("<H", seq)+bytes([len(data) if declared is None else declared])+data.ljust(32,b"\0")[:32]

def payload_result(cmd, data, version=1):
    if version != 1: return 4
    if len(data) > 32: return 2
    if cmd not in oracle.MESSAGE_LENGTHS: return 1
    if len(data) != oracle.MESSAGE_LENGTHS[cmd]: return 2
    try:
        oracle.decode_message({"version": version, "cmd": cmd, "seq": 0, "data": data})
    except ValueError:
        return 3
    return 0

for cmd in list(oracle.MESSAGE_LENGTHS)+[0x00,0xFE,0xFF]:
    for length in range(34):
        data = bytes(range(length))
        check("payload validity cmd%02X length%02d" % (cmd,length), lambda c=cmd,d=data:
              assert_equal(call("Test_Validate", put(packed(c,d,declared=len(d)))), payload_result(c,d)))
for frame in golden:
    check("valid golden payload cmd%02X" % frame[3], lambda f=frame: assert_equal(
        call("Test_Validate", put(packed(f[3],f[7:-2]))), payload_result(f[3], f[7:-2])))
boundary_payloads = [
    (0x01, b"\x01\x00\x00\x00\x58\x1B"),
    (0x01, b"\x01\x00\x80\x00\x11\x27"),
    (0x10, b"\x00\x00\x10\x27"), (0x10, b"\x01\x00\x11\x27"),
    (0x10, b"\xFF\xFF\x10\x27"), (0x10, b"\x01\x00\x00\x00"),
    (0x11,b"\x00"), (0x11,b"\x01"), (0x11,b"\x03"), (0x11,b"\x04"),
    (0x7F,b"\x00\x00\xFF\xFF"), (0x7F,b"\xFF\xFF\xFF\xFF"),
    (0x83,b"\xFF\xFF\xFF\xFF\x10\x27\xFF\xFF"),
    (0x83,b"\xFF\xFF\xFF\xFF\x11\x27\xFF\xFF"),
]
for index,(cmd,data) in enumerate(boundary_payloads):
    check("payload field boundary %02d" % index, lambda c=cmd,d=data:
          assert_equal(call("Test_Validate",put(packed(c,d))),payload_result(c,d)))
check("validator unsupported version", lambda: assert_equal(call("Test_Validate",put(packed(0x12,version=2))),4))
check("validator null pointer", lambda: assert_equal(call("K230_ValidatePayload",0),3))
check("encoder length33 returns zero", lambda: assert_equal(call("Test_Encode",0xFE,0,put(bytes(33)),33,64),0))
check("encoder insufficient capacity returns zero", lambda: assert_equal(call("Test_Encode",0x12,0,0,0,8),0))
check("encoder nonempty null DATA returns zero", lambda: assert_equal(call("Test_Encode",0x11,0,0,1,64),0))

# These expectations are application requirements, not a Python policy clone.
IGNORED, AUTH, REMOVED, UNKNOWN, READY, ERROR, STATUS, PONG, BAD = range(9)
def policy_init(): call("Test_PolicyInit")
def policy_call(cmd, seq, expected_action, data=None, result=None, latch=None):
    if data is None:
        data = {0x10:struct.pack("<HH",1,8750), 0x11:b"\x01",
                0x01:b"\x01\x00\x80\x00\x58\x1B",
                0x7F:b"\x01\x00\x00\x00",
                0x83:b"\x02\x03\x01\x00\x2E\x22\x00\x00"}.get(cmd,b"")
    action=call("Test_PolicyProcess",put(packed(cmd,data,seq)))
    event=bytes(machine.mem_read(symbols["test_event"],14))
    assert action == expected_action, (cmd,seq,action,expected_action)
    if action == AUTH:
        assert event[:2] == bytes([2,result])
        assert event[2:6] == bytes(4), "face identity reused RFID UID"
        assert event[6:10] == data, "face dedicated identity/score fields differ"
    else:
        assert event[:10] == b"\xA5"*10, "diagnostic/ignored frame modified AuthEvent"
    if latch is not None: assert event[10] == latch, (event[10],latch)
    return event

def trace(actions):
    policy_init()
    for action in actions:
        if action == "loss": call("Test_PolicyLoss")
        else: policy_call(*action)

check("policy user1 granted once; continuous success latched", lambda: trace(
    [(0x10,10,AUTH,None,1,1),(0x10,11,IGNORED,None,None,1),(0x10,12,IGNORED)]))
for user in (2,3,256,65535):
    check("policy unauthorized user %d denied and latched" % user, lambda uid=user: trace(
        [(0x10,1,AUTH,struct.pack("<HH",uid,9999),0,1),(0x10,2,IGNORED)]))
check("policy duplicate and replay seq suppressed including old NO_FACE", lambda: trace(
    [(0x10,100,AUTH,None,1),(0x10,100,IGNORED),(0x12,99,IGNORED,None,None,1),
     (0x12,100,IGNORED),(0x10,101,IGNORED),(0x12,102,REMOVED,None,None,0),
     (0x10,101,IGNORED),(0x10,103,AUTH,None,1)]))
check("policy UNKNOWN followed by successful identity can authenticate initially", lambda: trace(
    [(0x11,1,UNKNOWN,None,None,0),(0x11,2,IGNORED),(0x10,3,AUTH,None,1,1)]))
check("policy success UNKNOWN success keeps latch until NO_FACE", lambda: trace(
    [(0x10,1,AUTH,None,1),(0x11,2,UNKNOWN,None,None,1),(0x10,3,IGNORED),
     (0x12,4,REMOVED,None,None,0),(0x10,5,AUTH,None,1)]))
check("policy leave reenter grants separate visit; UID remains zero", lambda: trace(
    [(0x10,1,AUTH,None,1),(0x12,2,REMOVED),(0x10,3,AUTH,None,1)]))
check("policy uint16 async SEQ wraps 65535 to zero", lambda: trace(
    [(0x10,65534,AUTH,None,1),(0x12,65535,REMOVED),(0x10,0,AUTH,None,1),
     (0x12,65535,IGNORED,None,None,1),(0x12,1,REMOVED)]))
check("policy half window rejects ambiguous forward delta 32768", lambda: trace(
    [(0x10,10,AUTH,None,1),(0x12,32778,IGNORED,None,None,1),(0x12,11,REMOVED)]))

def status_does_not_auth_or_advance():
    policy_init()
    policy_call(0x11,10,UNKNOWN)
    event=policy_call(0x83,60000,STATUS,latch=0)
    assert event[12:14] == struct.pack("<H",10)
    event=policy_call(0x82,40000,PONG,latch=0)
    assert event[12:14] == struct.pack("<H",10)
    policy_call(0x10,11,AUTH,result=1)
    policy_call(0x83,2,STATUS,latch=1)
    policy_call(0x10,12,IGNORED)
check("policy historical STATUS success and PONG are diagnostics and preserve async SEQ",status_does_not_auth_or_advance)
check("policy MODULE_READY restart resets SEQ but requires leave", lambda: trace(
    [(0x01,0,READY,None,None,0),(0x10,1,AUTH,None,1),(0x12,2,REMOVED),
     (0x01,0,READY,None,None,1),(0x10,1,IGNORED),(0x12,2,REMOVED),
     (0x10,3,AUTH,None,1)]))
check("policy duplicate initial MODULE_READY does not relock", lambda: trace(
    [(0x01,0,READY,None,None,0),(0x01,0,IGNORED,None,None,0),(0x10,1,AUTH,None,1)]))
check("policy ERROR requires valid new NO_FACE before auth", lambda: trace(
    [(0x11,10,UNKNOWN),(0x7F,50000,ERROR,None,None,1),(0x10,11,IGNORED),
     (0x12,10,IGNORED,None,None,1),(0x12,12,REMOVED),(0x10,13,AUTH,None,1)]))
check("policy receive loss requires valid new NO_FACE before auth", lambda: trace(
    [(0x11,10,UNKNOWN),"loss",(0x10,11,IGNORED),(0x11,12,IGNORED),
     (0x83,60000,STATUS,None,None,1),(0x12,9,IGNORED),(0x12,13,REMOVED),
     (0x10,14,AUTH,None,1)]))
check("policy loss with missing READY accepts new low-sequence NO_FACE", lambda: trace(
    [(0x10,100,AUTH,None,1),"loss",(0x83,50000,STATUS,None,None,1),
     (0x11,0,UNKNOWN,None,None,1),(0x12,1,REMOVED,None,None,0),
     (0x10,2,AUTH,None,1,1)]))
check("policy invalid payload and unknown CMD never authorize", lambda: trace(
    [(0x10,0,BAD,b"\x00\x00\x2E\x22",None,0),(0xFE,1,BAD,b"hello",None,0),
     (0x80,2,IGNORED,None,None,0),(0x81,3,IGNORED,None,None,0),(0x10,4,AUTH,None,1)]))

failed=[record for record in records if not record["passed"]]
source_paths=SOURCES+[ROOT/"Drivers/Inc/k230_protocol.h",ROOT/"Services/Inc/face_auth.h",
                      ROOT/"App/Inc/app_events.h",ROOT/"Config/app_config.h",args.python_protocol]
report={"passed":len(records)-len(failed),"failed":len(failed),
        "seconds":round(time.monotonic()-started,2),
        "method":"Actual production Cortex-M3 C machine code executed in Unicorn; Python framing/payload oracle",
        "hardware":"No serial ports opened; no UI; no target flashed; no HAL/RTOS/ISR or physical wiring validation",
        "compiler":str(args.compiler),"oracle":str(args.python_protocol),
        "source_sha256":{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths},
        "golden_vectors":[frame.hex() for frame in golden],"cases":records}
(args.output_root/"software_test_results.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
summary="PASS %d; FAIL %d; %.2fs. Actual ARM C code; software only." % (report["passed"],report["failed"],report["seconds"])
(args.output_root/"software_test_summary.txt").write_text(summary+"\n"+"\n".join(
    record["name"]+": "+record["error"] for record in failed)+"\n",encoding="utf-8")
print(summary)
raise SystemExit(1 if failed else 0)
