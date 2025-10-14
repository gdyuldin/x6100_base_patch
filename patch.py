import hashlib
import os
import shlex
import string
import subprocess
import sys

import numpy as np

# calc tx_audio: start patch s15 - result
#    08024ea0 67 ee 87 7a     vmul.   s15,s15,s14

# Modulation addr: 2000853c

# sbss: 20003560, sbss_p: 08034ab8
# ebss: 2000ea7c, ebss_p: 08034abc



prog_name = "x6100_mcu"

patchsets = {
    # X6100_BBFW_V1.1.6_221112001.bin
    '36eb378655ac5661a3e676a0b6caab02': {
        'date': '221112001',
        'stack_p_1': 0x08032dbc,
        'init_data': 0x08032dae,
        'configure': 0x08023c36,
        'apply_rx_iq_offset': 0x080241ac,
        'compress': 0x08024b06,
        'tx_amp': 0x08024b6e,
        'tx_coeff_calc': 0x080237ae,
        'am_fm_rx_process': 0x08027de0,
        'anf_update': 0x080251f0,
        'copy_flow': 0x08032128,
        'build_time': 0x0803b204,
        'external_fn': {
            'setup_biquad_filter': 0x080216f8,
            'arm_fill_f32': 0x08032dd8,
            'arm_biquad_cascade_df1_f32': 0x0803436c,
            'arm_sqrt_f32': 0x0803a41c,
            'arm_copy_f32': 0x08032e14,
            'print_str': 0x08035bf4,
        },
        'end_oem_fw_offset': 0x0807df34,
    },
    # X6100_BBFW_V1.1.6_230307001.bin
    'f19fb85db1f74ad10eb379927880519c': {
        # STACK_P = 0x20003000
        # DATA_START = 0x20000000
        # BSS_START  = 0x20003560
        'date': '230307001',
        'stack_p_1': 0x08034a74,
        'init_data': 0x08034a66,
        'configure': 0x0802432c,
        'apply_rx_iq_offset': 0x0802494c,
        'compress': 0x08025388,
        'tx_amp': 0x080253fa,
        'tx_coeff_calc': 0x08023e18,
        'am_fm_rx_process': 0x08028b8e,
        'anf_update': 0x08025bc8,
        'copy_flow': 0x08033c88,
        'build_time': 0x0803cebc,
        'external_fn': {
            'setup_biquad_filter': 0x08021764,
            'arm_fill_f32': 0x08034a90,
            'arm_biquad_cascade_df1_f32': 0x08036024,
            'print_str': 0x080378ac,
            'arm_sqrt_f32': 0x0803c0d4,
            'arm_copy_f32': 0x08034acc,
            'arm_fir_decimate_f32': 0x08035cac,
        },
        'end_oem_fw_offset': 0x807fbf4,
        'filter_data': {
            'ssb_cw_decim_interp': {
                'coeffs_addr': 0x0803d82c,
                'numTaps': 64,
                'file': 'fir_ssb_cw_8.txt'
            },
            'am_fm_decim_interp': {
                'coeffs_addr': 0x0803d92c,
                'numTaps': 36,
                'file': 'fir_am_fm_4.txt'
            }
        }
    },
}


def make_stm32f_ld_file(patchset: dict):
    with open("stm32f427zgtx_flash.ld.format") as f:
        tpl = string.Template(f.read())

    external_fn = sorted(patchset['external_fn'].items(), key=lambda x: x[1])
    external_functions = []
    for fn_name, addr in external_fn:
        external_functions.append(f". = 0x{addr:08x} - start_text;")
        external_functions.append(f"*(.{fn_name}_sec)")
    mapping = {
        "EXTERNAL_FUNCTIONS": "\n".join(external_functions),
        "END_OEM_FW_OFFSET": patchset['end_oem_fw_offset'],
    }
    text = tpl.substitute(mapping)

    with open("stm32f427zgtx_flash.ld", "w") as f:
        f.write(text)


def compile_c_binaries(date):
    cmd = "mkdir -p build/Release"
    subprocess.check_call(shlex.split(cmd))
    os.chdir("build/Release")
    cmd = f"cmake -DCMAKE_BUILD_TYPE=Release -DTARGET_BUILD_DATE={date} ../.."
    subprocess.check_call(shlex.split(cmd))
    # TODO in this time always full build
    subprocess.check_call(["make", "clean"])
    subprocess.check_call("make")
    # TODO check build result
    os.chdir("../..")

def compile_patch_helper(asm, o_file, date):
    cmd = f"arm-none-eabi-as {asm} -o {o_file} --defsym BUILD_DATE={date}"
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


def get_block_start_end(name, section_prefix="text"):
    with open(f"build/Release/{prog_name}.map") as f:
        while True:
            line = next(f)
            if line.strip().startswith(f".{section_prefix}.{name}"):
                if len(line.split()) == 1:
                    line = next(f)
                else:
                    line = line.split(None, 1)[1]
                parts = line.split()
                start_addr = int(parts[0], 16)
                end_addr = start_addr + int(parts[1], 16)
                print(f".{section_prefix}.{name} start: {start_addr:x}, end: {end_addr:x}, len: {(end_addr - start_addr):x}")
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


class InjectFunction:
    def __init__(self, name, inject_addr, copy_replaced=False, return_addr=None):
        self.name = name
        self.inject_addr = inject_addr
        self.copy_replaced = copy_replaced
        self.start_addr, self.end_addr = get_block_start_end(name)
        self.wrapper_len = 0
        self.insert_code = b""
        if return_addr is None:
            self.return_addr = inject_addr + 4
        else:
            self.return_addr = return_addr

    def setup_wrapper_len(self, asm_o_file):
        self.wrapper_len = align(len(get_patched_section(asm_o_file, f".{self.name}_wrapper")))

    def print_used_registers(self):
        print(f"Registers for {self.name}")
        print_used_registers(fn_name=self.name)

    def setup_insert_code(self, asm_elf):
        self.insert_code = get_patched_section(asm_elf, f".insert_to_{self.name}")
        assert len(self.insert_code) == 4, f"{self.name} insert is not 4 bytes"


class InjectFunctions:
    def __init__(self, functions: list[InjectFunction], asm_o_file, flash_offset, orig_fw_size, rodata_start, rodata_end) -> None:
        self.functions = functions
        self.flash_offset = flash_offset
        self.sections = {}
        self.rodata_start = rodata_start
        self.rodata_end = rodata_end
        self.new_code_end = rodata_end
        accumulated_size = 0
        for fn in self.functions:
            fn.setup_wrapper_len(asm_o_file)
            fn.print_used_registers()
            self.sections.update({
                f"insert_to_{fn.name}": fn.inject_addr,
                f"{fn.name}_wrapper": orig_fw_size + flash_offset + accumulated_size,
                fn.name: fn.start_addr,
            })
            accumulated_size += fn.wrapper_len
            self.new_code_end = max(self.new_code_end, fn.end_addr)

    def setup_insert_code(self, asm_elf):
        for fn in self.functions:
            fn.setup_insert_code(asm_elf)

    def copy_code(self, src: bytes, dst: bytearray):
        for fn in self.functions:
            start = fn.start_addr - self.flash_offset
            end = fn.end_addr - self.flash_offset
            dst[start: end] = src[start: end]

    def copy_rodata(self, src: bytes, dst: bytearray):
        start = self.rodata_start - self.flash_offset
        end = self.rodata_end - self.flash_offset
        dst[start: end] = src[start: end]

    def copy_wrappers(self, asm_elf, dst: bytearray):
        for fn in self.functions:
            sec_name = f"{fn.name}_wrapper"
            start = self.sections[sec_name] - self.flash_offset
            code = get_patched_section(asm_elf, f".{sec_name}")
            end = start + len(code)
            dst[start: end] = code

    def insert_jumps(self, dst: bytearray):
        for fn in self.functions:
            from_sec_name = f"insert_to_{fn.name}"
            to_sec_name = f"{fn.name}_wrapper"
            from_offset = self.sections[from_sec_name] - self.flash_offset
            to_offset = self.sections[to_sec_name] - self.flash_offset

            # Copy instructions
            if fn.copy_replaced:
                dst[to_offset: to_offset + 4] = dst[from_offset: from_offset + 4]
            dst[from_offset: from_offset + 4] = fn.insert_code

            # fill gap with nop
            gap = b"bf00" * ((fn.return_addr - fn.inject_addr - 4) // 2)
            dst[from_offset + 4: from_offset + 4 + len(gap)] = gap


def update_filters(dst: bytearray, flash_offset, addr, path, numTaps):
    data = np.loadtxt(path, dtype=np.float32)
    assert len(data) == numTaps
    # Reverse (for CMSIS-DSP)
    data = data[::-1]
    data = data.tobytes()
    dst[addr - flash_offset: addr - flash_offset + len(data)] = data


def main():
    with open(sys.argv[1], "rb") as f:
        orig_code = f.read()

    hashsum = hashlib.md5(orig_code).hexdigest()
    patchset = patchsets[hashsum]

    make_stm32f_ld_file(patchset)
    compile_c_binaries(date=patchset["date"])

    with open(f"build/Release/{prog_name}.bin", "rb") as f:
        patched_code = f.read()

    # check from ghidra
    flash_offset = 0x08020000

    # value from reset_handler and first 4 bytes from firmware
    stack_p_addr = 0x20030000
    stack_p_0 = flash_offset
    stack_p_1 = patchset["stack_p_1"]
    # 688 is a len of data struct for injected functions
    stack_new_p = stack_p_addr - 9744

    # arm-none-eabi-objdump -S build/Release/CMakeFiles/test_patch.dir/Core/Src/compressor.c.obj

    asm = "asm/helper.s"
    o_file = "asm/helper.o"
    compile_patch_helper(asm, o_file, date=patchset["date"])

    rodata_start, rodata_end = get_block_start_end("patch_data", "rodata")

    functions = InjectFunctions([
        InjectFunction("init_data", patchset["init_data"]),  # fill ram area with zeros
        InjectFunction("configure", patchset["configure"]),  # configure state at start of DMA handler
        InjectFunction("apply_rx_iq_offset", patchset["apply_rx_iq_offset"]),  # Convert IQ to float and apply an offsets
        InjectFunction("compress", patchset["compress"], copy_replaced=True),  # compress, limit TX signal
        InjectFunction("tx_amp", patchset["tx_amp"]),  # amp IQ according to configured TX power
        InjectFunction("tx_coeff_calc", patchset["tx_coeff_calc"]),  # update coefficients for IQ on TX power change
        InjectFunction("am_fm_rx_process", patchset["am_fm_rx_process"]),  # process AM/FM rx (sql, dc blocker)
        InjectFunction("anf_update", patchset["anf_update"]),  # update notch filter params
        InjectFunction("copy_flow", patchset["copy_flow"]),  # copy data samples to flow with changes
    ], asm_o_file=o_file, flash_offset=flash_offset, orig_fw_size=len(orig_code), rodata_start=rodata_start, rodata_end=rodata_end)

    elf = "asm/helper.elf"
    link_patch_helper(o_file, elf, flash_offset, **functions.sections)

    functions.setup_insert_code(elf)

    dst = bytearray(functions.new_code_end - flash_offset)
    # Copy original code
    dst[:len(orig_code)] = orig_code

    # copy new_blocks
    functions.copy_code(patched_code, dst)
    # copy rodata
    functions.copy_rodata(patched_code, dst)

    #update stack pointers
    for stack_p in (stack_p_0, stack_p_1):
        offset = stack_p - flash_offset
        dst[offset: offset + 4] = np.uint32(stack_new_p).tobytes()

    # copy wrappers
    functions.copy_wrappers(elf, dst)

    # insert jumps
    functions.insert_jumps(dst)

    # Update filters
    for name, filter_params in patchset["filter_data"].items():
        print("Update filters for", name)
        update_filters(dst,
            flash_offset,
            filter_params['coeffs_addr'],
            filter_params['file'], filter_params['numTaps']
        )

    ver = "r8"
    build_time = ver.encode() + bytes(11 - len(ver))
    assert len(build_time) < 12
    build_time_addr = patchset['build_time'] - flash_offset
    print(build_time_addr)
    dst[build_time_addr: build_time_addr + len(build_time)] = build_time

    fn, ext = os.path.splitext(sys.argv[1])
    dst_file = f"{fn}_{ver}{ext}"
    with open(dst_file, "wb") as f:
        f.write(dst)
    print(f"{dst_file} saved")


if __name__ == "__main__":
    main()
