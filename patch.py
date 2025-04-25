import shlex
import pathlib
import subprocess
import numpy as np

# calc tx_audio: start patch s15 - result
#    08024ea0 67 ee 87 7a     vmul.   s15,s15,s14

# Modulation addr: 2000853c

# sbss: 20003560, sbss_p: 08034ab8
# ebss: 2000ea7c, ebss_p: 08034abc



prog_name = "x6100_mcu"


def compile_pathch_helper(asm, o_file):
    cmd = f"arm-none-eabi-as {asm} -o {o_file}"
    print("Call:", cmd)
    subprocess.check_call(shlex.split(cmd))


def link_patch_helper(o_file, target, start_offset, **sections):
    cmd = f"arm-none-eabi-ld -Ttext={hex(start_offset)}"
    for sec_name, sec_addr in sections.items():
        cmd += f" --section-start .{sec_name}={hex(sec_addr)} "
    cmd += f"-o {target} {o_file}"
    print("Call:", cmd)
    ld_cmd = shlex.split(cmd)
    subprocess.check_call(ld_cmd)


def get_patched_section(elf, section_name) -> bytes:
    cmd = shlex.split(f"arm-none-eabi-objcopy {elf} /dev/null --dump-section {section_name}=/dev/stdout")
    res = subprocess.check_output(cmd)
    return res


def extend_bss(code, ebss_p, add_size):
    ebss = np.frombuffer(code[ebss_p: ebss_p + 4], dtype=np.uint32)
    ebss += add_size
    code[ebss_p: ebss_p + 4] = ebss.tobytes()
    return ebss

def get_comp_struct_addr_p(placeholder=b"deadbeef"):
    cmd = shlex.split(f"arm-none-eabi-objdump -S -z build/Release/{prog_name}.elf")
    res = subprocess.check_output(cmd)
    addr = None
    in_comp_fn = False
    for line in res.splitlines():
        if not in_comp_fn and line.endswith(b" <compress>:"):
            in_comp_fn = True
            continue
        if not in_comp_fn:
            continue
        if not line.strip():
            break
        if b".word" not in line:
            continue
        parts = line.split()
        if parts[1] == placeholder:
            if addr is not None:
                raise RuntimeError("2 variables is not supported yet")
            addr = int(parts[0][:-1], 16)
    if addr is None:
        raise RuntimeError("Variable addr is not found")
    return addr


def get_func_start_end(name):
    with open(f"build/Release/{prog_name}.map") as f:
        while True:
            line = next(f)
            if line.strip().startswith(f".text.{name}"):
                if len(line.split()) == 1:
                    line = next(f)
                else:
                    line = line.split(None, 1)[1]
                parts = line.split()
                start_addr = int(parts[0], 16)
                end_addr = start_addr + int(parts[1], 16)
                print(f"func {name} start: {start_addr:x}, end: {end_addr:x}, len: {(end_addr - start_addr):x}")
                return start_addr, end_addr

def align(addr, val=4):
    return int(np.ceil(addr / val) * val)


def print_used_registers(fn_name="compress"):
    output = subprocess.check_output(shlex.split('arm-none-eabi-objdump -S -z build/Release/x6100_mcu.elf'))
    collect = False
    registers = {}
    for line in output.splitlines():
        line = line.decode()
        if line.endswith(f'<{fn_name}>:'):
            collect = True
            continue
        if not collect:
            continue
        if not line.strip():
            break
        if "\t.word\t" in line:
            continue
        if "\t.short\t" in line:
            continue
        parts = line.split("\t")
        if parts[2] == "bl":
            print("!!!bl call:", line)
        if len(parts) < 4:
            continue
        if parts[2] == 'it':
            continue
        cmd_args = parts[3]
        cmd_args = cmd_args.split(',')[0]
        if fn_name in cmd_args:
            continue
        if cmd_args.startswith('APSR_'):
            continue
        cmd_args = cmd_args.strip("{}[]")
        if cmd_args not in registers:
            registers[cmd_args] = line
    registers = sorted(registers.items(), key=lambda x: x[0])
    for k, l in registers:
        print(k, l)

def main():
    with open("firmwares/X6100_BBFW_V1.1.6_221112001_p160.bin", "rb") as f:
        orig_code = f.read()
    with open(f"build/Release/{prog_name}.bin", "rb") as f:
        patched_code = f.read()

    # get used registers
    print("Regisers for compress")
    print_used_registers(fn_name="compress")
    print("Regisers for tx_amp")
    print_used_registers(fn_name="tx_amp")
    print("Regisers for tx_coeff_calc")
    print_used_registers(fn_name="tx_coeff_calc")
    print("Regisers for am_fm_rx_process")
    print_used_registers(fn_name="am_fm_rx_process")

    # check from ghidra
    start_offset = 0x08020000

    # value from reset_handler and first 4 bytes from firmware
    stack_p_addr = 0x20030000
    stack_p_0 = start_offset
    stack_p_1 = 0x08032dbc
    # 592 is a len of struct for compressor
    stack_new_p = stack_p_addr - 592

    # somewhere within process
    inject_comp_addr = 0x08024b06
    # somewhere at begin of reset_handler
    inject_mem_addr  = 0x08032dae

    inject_tx_amp_addr = 0x08024b6e

    inject_tx_coeff_calc_addr = 0x080237ae
    inject_am_fm_rx_process_addr = 0x08027de0


    compress_start, compress_end = get_func_start_end("compress")
    fill_mem_start, fill_mem_end = get_func_start_end("fill_zero")
    tx_amp_start, tx_amp_end = get_func_start_end("tx_amp")
    tx_coeff_calc_start, tx_coeff_calc_end = get_func_start_end("tx_coeff_calc")
    am_fm_rx_process_start, am_fm_rx_process_end = get_func_start_end("am_fm_rx_process")


    # arm-none-eabi-objdump -S build/Release/CMakeFiles/test_patch.dir/Core/Src/compressor.c.obj

    asm = "asm/helper.s"
    o_file = "asm/helper.o"
    elf = "asm/helper.elf"
    compile_pathch_helper(asm, o_file)

    comp_wrapper_len = align(len(get_patched_section(o_file, ".comp_wrapper")))
    fill_mem_wrapper_len = align(len(get_patched_section(o_file, ".fill_mem_wrapper")))
    tx_amp_wrapper_len = align(len(get_patched_section(o_file, ".tx_amp_wrapper")))
    tx_coeff_calc_wrapper_len = align(len(get_patched_section(o_file, ".tx_coeff_calc_wrapper")))

    sections = {
        "insert_to_tx_process": inject_comp_addr,
        "comp_wrapper": len(orig_code) + start_offset,
        "compressor": compress_start,

        "insert_to_reset_handler": inject_mem_addr,
        "fill_mem_wrapper": len(orig_code) + comp_wrapper_len + start_offset,
        "fill_mem": fill_mem_start,

        "insert_to_tx_amp": inject_tx_amp_addr,
        "tx_amp_wrapper": len(orig_code) + comp_wrapper_len + fill_mem_wrapper_len + start_offset,
        "tx_amp": tx_amp_start,

        "insert_to_tx_coeff_calc": inject_tx_coeff_calc_addr,
        "tx_coeff_calc_wrapper": len(orig_code) + comp_wrapper_len + fill_mem_wrapper_len + tx_amp_wrapper_len + start_offset,
        "tx_coeff_calc": tx_coeff_calc_start,

        "insert_to_am_fm_rx_process": inject_am_fm_rx_process_addr,
        "am_fm_rx_process_wrapper": len(orig_code) + comp_wrapper_len + fill_mem_wrapper_len +
            tx_amp_wrapper_len + tx_coeff_calc_wrapper_len + start_offset,
        "am_fm_rx_process": am_fm_rx_process_start,
    }

    link_patch_helper(o_file, elf, start_offset, **sections)

    insert_to_tx_code = get_patched_section(elf, ".insert_to_tx_process")
    assert len(insert_to_tx_code) == 4
    insert_to_reset_handler_code = get_patched_section(elf, ".insert_to_reset_handler")
    assert len(insert_to_reset_handler_code) == 4
    insert_to_tx_amp_code = get_patched_section(elf, ".insert_to_tx_amp")
    assert len(insert_to_tx_amp_code) == 4
    insert_to_tx_coeff_calc_code = get_patched_section(elf, ".insert_to_tx_coeff_calc")
    assert len(insert_to_tx_coeff_calc_code) == 4
    insert_to_am_fm_rx_process_code = get_patched_section(elf, ".insert_to_am_fm_rx_process")
    assert len(insert_to_am_fm_rx_process_code) == 4

    # comp_wrapper_code = get_patched_section(elf, ".comp_wrapper")
    # fill_mem_wrapper_code = get_patched_section(elf, ".fill_mem_wrapper")

    # assert len(comp_wrapper_code) + len(orig_code) + start_offset < min(compress_start, fill_mem_start)

    dst = bytearray(max(compress_end, fill_mem_end, tx_amp_end, tx_coeff_calc_end, am_fm_rx_process_end) - start_offset)
    # Copy original code
    dst[:len(orig_code)] = orig_code

    # copy new_block
    for start, end in [
            (compress_start, compress_end),
            (fill_mem_start, fill_mem_end),
            (tx_amp_start, tx_amp_end),
            (tx_coeff_calc_start, tx_coeff_calc_end),
            (am_fm_rx_process_start, am_fm_rx_process_end),
        ]:
        start = start - start_offset
        end = end - start_offset
        dst[start: end] = patched_code[start: end]

    # update function static var addr
    # dst[compress_struct_p_addr - start_offset: compress_struct_p_addr - start_offset + 4] = np.uint32(stack_new_p).tobytes()

    #update stack pointers
    for stack_p in (stack_p_0, stack_p_1):
        offset = stack_p - start_offset
        dst[offset: offset + 4] = np.uint32(stack_new_p).tobytes()

    # copy wrappers
    for sec_name in ("comp_wrapper", "fill_mem_wrapper", "tx_amp_wrapper", "tx_coeff_calc_wrapper", "am_fm_rx_process_wrapper"):
        start = sections[sec_name] - start_offset
        code = get_patched_section(elf, f".{sec_name}")
        end = start + len(code)
        dst[start: end] = code

    # insert jumps
    for from_sec_name, to_sec_name in (
            ("insert_to_tx_process", "comp_wrapper"),
            ("insert_to_reset_handler", "fill_mem_wrapper"),
            ("insert_to_tx_amp", "tx_amp_wrapper"),
            ("insert_to_tx_coeff_calc", "tx_coeff_calc_wrapper"),
            ("insert_to_am_fm_rx_process", "am_fm_rx_process_wrapper"),
        ):
        from_offset = sections[from_sec_name] - start_offset
        to_offset = sections[to_sec_name] - start_offset
        jump_code = get_patched_section(elf, f".{from_sec_name}")

        # Copy instructions
        if to_sec_name not in ("fill_mem_wrapper", "tx_amp_wrapper", "tx_coeff_calc_wrapper", "am_fm_rx_process_wrapper"):
            dst[to_offset: to_offset + 4] = dst[from_offset: from_offset + 4]
        dst[from_offset: from_offset + 4] = jump_code

    ver = "r2"
    build_time = ver.encode() + bytes(11 - len(ver))
    assert len(build_time) < 12
    build_time_addr = 0x0803b204 - start_offset
    dst[build_time_addr: build_time_addr+len(build_time)] = build_time

    with open(f"firmwares/X6100_BBFW_V1.1.6_221112001_{ver}.bin", "wb") as f:
        f.write(dst)




if __name__ == "__main__":
    main()
