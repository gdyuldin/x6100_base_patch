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

def get_comp_struct_variable_offset():
    cmd = shlex.split(f"arm-none-eabi-objdump -S -z build/Release/CMakeFiles/{prog_name}.dir/Core/Src/compressor.c.obj")
    res = subprocess.check_output(cmd)
    offset = None
    for line in res.splitlines():
        if b".word" not in line:
            continue
        parts = line.split()
        if parts[1] == b"00000000":
            if offset is not None:
                raise RuntimeError("2 variables is not supported yet")
            offset = int(parts[0][:-1], 16)
    if offset is None:
        raise RuntimeError("Variable offset is not found")
    return offset


def get_func_start_end(name):
    with open(f"build/Release/{prog_name}.map") as f:
        while True:
            line = next(f)
            if f".text.{name}" == line.strip():
                line = next(f)
                parts = line.split()
                start_addr = int(parts[0], 16)
                end_addr = start_addr + int(parts[1], 16)
                return start_addr, end_addr

def align(addr, val=4):
    return int(np.ceil(addr / val) * val)


def main():
    with open("X6100_BBFW_V1.1.8_240915003_160hz_fm.bin", "rb") as f:
        orig_code = f.read()
    with open(f"build/Release/{prog_name}.bin", "rb") as f:
        patched_code = f.read()

    # check from ghidra
    start_offset = 0x08020000

    # value from reset_handler and first 4 bytes from firmware
    stack_p_addr = 0x20030000
    stack_p_0 = start_offset
    stack_p_1 = 0x08034aa8
    # 256 is a len of struct for compressor
    stack_new_p = stack_p_addr - 256

    # somewhere within process
    inject_comp_addr = 0x08024e70
    # somewhere at begin of reset_handler
    inject_mem_addr  = 0x08034a9a

    compress_start, compress_end = get_func_start_end("compress")
    fill_mem_start, fill_mem_end = get_func_start_end("fill_zero")


    # arm-none-eabi-objdump -S build/Release/CMakeFiles/test_patch.dir/Core/Src/compressor.c.obj
    compress_struct_p_addr = compress_start + get_comp_struct_variable_offset()

    asm = "asm/helper.s"
    o_file = "asm/helper.o"
    elf = "asm/helper.elf"
    compile_pathch_helper(asm, o_file)

    comp_wrapper_len = align(len(get_patched_section(o_file, ".comp_wrapper")))

    sections = {
        "insert_to_tx_process": inject_comp_addr,
        "comp_wrapper": len(orig_code) + start_offset,
        "compressor": compress_start,

        "insert_to_reset_handler": inject_mem_addr,
        "fill_mem_wrapper": len(orig_code) + comp_wrapper_len + start_offset,
        "fill_mem": fill_mem_start,
    }

    link_patch_helper(o_file, elf, start_offset, **sections)

    insert_to_tx_code = get_patched_section(elf, ".insert_to_tx_process")
    assert len(insert_to_tx_code) == 4
    insert_to_reset_handler_code = get_patched_section(elf, ".insert_to_reset_handler")
    assert len(insert_to_reset_handler_code) == 4

    # comp_wrapper_code = get_patched_section(elf, ".comp_wrapper")
    # fill_mem_wrapper_code = get_patched_section(elf, ".fill_mem_wrapper")

    # assert len(comp_wrapper_code) + len(orig_code) + start_offset < min(compress_start, fill_mem_start)

    dst = bytearray(max(compress_end, fill_mem_end) - start_offset)
    # Copy original code
    dst[:len(orig_code)] = orig_code

    # copy new_block
    for start, end in [(compress_start, compress_end), (fill_mem_start, fill_mem_end)]:
        start = start - start_offset
        end = end - start_offset
        dst[start: end] = patched_code[start: end]

    # update function static var addr
    dst[compress_struct_p_addr - start_offset: compress_struct_p_addr - start_offset + 4] = np.uint32(stack_new_p).tobytes()

    #update stack pointers
    for stack_p in (stack_p_0, stack_p_1):
        offset = stack_p - start_offset
        dst[offset: offset + 4] = np.uint32(stack_new_p).tobytes()

    # copy wrappers
    for sec_name in ("comp_wrapper", "fill_mem_wrapper"):
        start = sections[sec_name] - start_offset
        code = get_patched_section(elf, f".{sec_name}")
        end = start + len(code)
        dst[start: end] = code

    # insert jumps
    for from_sec_name, to_sec_name in (("insert_to_tx_process", "comp_wrapper"), ("insert_to_reset_handler", "fill_mem_wrapper")):
        from_offset = sections[from_sec_name] - start_offset
        to_offset = sections[to_sec_name] - start_offset
        jump_code = get_patched_section(elf, f".{from_sec_name}")

        # Copy instructions
        if to_sec_name != "fill_mem_wrapper":
            dst[to_offset: to_offset + 4] = dst[from_offset: from_offset + 4]
        dst[from_offset: from_offset + 4] = jump_code

    build_time = b"160Hz FM C"
    assert len(build_time) < 12
    dst[0x1ce00: 0x1ce00+len(build_time)] = build_time

    with open("patched.bin", "wb") as f:
        f.write(dst)




if __name__ == "__main__":
    main()
