import dataclasses
import hashlib
import math
import os
import shlex
import string
import subprocess
import sys
import tempfile

import numpy as np

# calc tx_audio: start patch s15 - result
#    08024ea0 67 ee 87 7a     vmul.   s15,s15,s14

# Modulation addr: 2000853c

# sbss: 20003560, sbss_p: 08034ab8
# ebss: 2000ea7c, ebss_p: 08034abc


@dataclasses.dataclass
class Insert:
    name: str
    desired_len: int = 4


prog_name = "x6100_mcu"

patchsets = {
    # X6100_BBFW_V1.1.6_221112001.bin
    '36eb378655ac5661a3e676a0b6caab02': {
        'date': '221112001',
        'stack_p_1': 0x08032dbc,
        'init_data': 0x08032dae,
        'configure': 0x08023c36,
        # 'apply_rx_iq_offset': 0x080241ac,
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
        'dma_end': 0x08025b72,
        'remove_iq_offset': 0x0802492c,
        # 'apply_rx_iq_offset': 0x0802494c,
        'if_shift_rx': 0x08024ac8,
        'if_shift_tx': 0x0802d320,
        'compress': 0x0802539a, # TX
        'am_modulation': 0x08028608,  # TX
        'fm_modulate': 0x08028634,  # TX
        'tx_amp': 0x080253fa,
        'tx_coeff_calc': 0x08023e18,
        'fm_demodulate': 0x08028b56,  # RX
        'am_fm_rx_process': 0x08028b8e,  # RX
        'anf_update': 0x08025bc8,  # RX

        'nr_apply': 0x08024bf6,
        'skip_oem_nr': 0x08024d42,
        'skip_oem_nr_postprocess': 0x08024da8,

        'nb_apply': 0x08024962,

        'copy_flow': 0x08033c88,
        'process_i2c_cmd': 0x0802c1a0,
        'skip_am_mult': 0x08024d20,

        'ssb_iq_filter1': 0x08026f92,
        'ssb_iq_filter2': 0x08026fbe,

        'vox_update': 0x08024f20,
        'vox_restore_audio_input': 0x0802bcbe,
        'aic_setup_adc_dc_blocker': 0x0802fc6a,

        'main_board_output_lvl': 0x0803465e,

        'build_time': 0x0803cebc,
        'external_fn': {
            'setup_biquad_filter': 0x08021764,
            'arm_fill_f32': 0x08034a90,
            'arm_biquad_cascade_df1_f32': 0x08036024,
            'print_str': 0x080378ac,
            'arm_sqrt_f32': 0x0803c0d4,
            'arm_copy_f32': 0x08034acc,
            'arm_fir_decimate_f32': 0x08035cac,
            'arm_sin_f32': 0x08036394,
            'arm_cos_f32': 0x0803641c,
            'write_i2c': 0x08031a7c,
            'setup_tx': 0x0802bbf4,
            'set_mic_level': 0x0802f6ce,
            'set_audio_codec_input': 0x0802f9ee,
            'setup_internal_mic_power': 0x0802fb22,
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
            },
            'fir_decim_2_am_fm': {
                'coeffs_addr': 0x0803d9bc,
                'numTaps': 36,
                'file': 'fir_decim_2.txt'
            }
        }
    },
}


def make_stm32f_ld_file(patchset: dict, end_orig_and_wrappers: int):
    with open("stm32f427zgtx_flash.ld.format") as f:
        tpl = string.Template(f.read())

    external_fn = sorted(patchset['external_fn'].items(), key=lambda x: x[1])
    external_functions = []
    for fn_name, addr in external_fn:
        external_functions.append(f". = 0x{addr:08x} - start_text;")
        external_functions.append(f"*(.{fn_name}_sec)")
    mapping = {
        "EXTERNAL_FUNCTIONS": "\n".join(external_functions),
        "END_OEM_FW_OFFSET": end_orig_and_wrappers,
    }
    text = tpl.substitute(mapping)

    with open("stm32f427zgtx_flash.ld", "w") as f:
        f.write(text)


def make_asm_ld_file(
    patchset: dict,
    inserts: list[Insert],
    asms: list[Insert],
    oem_fw_end: int,
    new_addresses: dict,
):
    with open("asm/patch.ld.tpl") as f:
        tpl = string.Template(f.read())

    ins_blocks = []
    all_ins = inserts + asms
    all_ins.sort(key=lambda x: patchset[x.name])
    for ins in all_ins:
        ins_blocks.append(f"\t\t. = 0x{patchset[ins.name]:08x} - start_text;")
        ins_blocks.append(f"\t\t*(.insert_to_{ins.name});")

    wrap_blocks = []
    for ins in inserts:
        wrap_blocks.append(f"\t\t*(.{ins.name}_wrapper)")

    inserts.sort(key=lambda x: new_addresses[x.name])
    new_fn_blocks = []
    for ins in inserts:
        new_fn_blocks.append(f"\t\t. = 0x{new_addresses[ins.name]:08x} - start_text;")
        new_fn_blocks.append(f"\t\t*(.{ins.name});")

    mapping = {
        "INSERTS": "\n".join(ins_blocks),
        "OEM_FW_END": f"0x{oem_fw_end:08x}",
        "WRAPPERS": "\n".join(wrap_blocks),
        "NEW_FN": "\n".join(new_fn_blocks),
    }
    text = tpl.substitute(mapping)

    with open("asm/patch.ld", "w") as f:
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


def get_patch_helper_sizes(o_file):
    cmd = f"arm-none-eabi-size -A {o_file}"
    print("Call:", cmd)
    res = subprocess.check_output(shlex.split(cmd)).decode()
    sections_sizes = {
        "inserts": {},
        "wrappers": {},
        "code_placeholders": {},
    }
    for line in res.splitlines():
        if not line.startswith("."):
            continue
        if line.strip() in (".text", ".data", ".bss", ".ARM.attributes"):
            continue
        section, size, _ = line.split()
        size = int(size)
        if section.startswith('.insert_to'):
            sections_sizes["inserts"][section] = size
        elif section.endswith('_wrapper'):
            sections_sizes["wrappers"][section] = size
        else:
            sections_sizes["code_placeholders"][section] = size
    return sections_sizes


def get_elf_addresses(elf_file):
    # Add -S for size
    cmd = f"arm-none-eabi-nm {elf_file}"
    print("Call:", cmd)
    res = subprocess.check_output(shlex.split(cmd)).decode()
    symbols = {}
    for line in res.splitlines():
        addr, _, name = line.split()
        addr = int(addr, 16)
        symbols[name] = addr
    return symbols


def link_patch_helper(o_file, target):
    cmd = f"arm-none-eabi-ld -Map={target}.map -T asm/patch.ld -o {target}.elf {o_file}"
    print("Call:", cmd)
    ld_cmd = shlex.split(cmd)
    subprocess.check_call(ld_cmd)
    cmd = f"arm-none-eabi-objcopy -O binary {target}.elf {target}.bin"
    print("Call:", cmd)
    ld_cmd = shlex.split(cmd)
    subprocess.check_call(ld_cmd)

# def link_patch_helper(o_file, target, start_offset, **sections):
#     cmd = f"arm-none-eabi-ld -Ttext={hex(start_offset)}"
#     for sec_name, sec_addr in sections.items():
#         if sec_addr is not None:
#             cmd += f" --section-start .{sec_name}={hex(sec_addr)} "
#     cmd += f"-o {target} {o_file}"
#     print("Call:", cmd)
#     ld_cmd = shlex.split(cmd)
#     subprocess.check_call(ld_cmd)


def get_patched_section(elf, section_name) -> bytes:
    cmd = shlex.split(f"arm-none-eabi-objcopy {elf} /dev/null --dump-section {section_name}=/dev/stdout")
    res = subprocess.check_output(cmd)
    return res


def get_block_start_end(name, section_prefix="text"):
    with open(f"build/Release/{prog_name}.map") as f:
        while True:
            line = next(f)
            line = line.strip()
            if line.startswith(f".{section_prefix}.{name} ") or line == f".{section_prefix}.{name}":
                if len(line.split()) == 1:
                    line = next(f)
                else:
                    line = line.split(None, 1)[1]
                parts = line.split()
                start_addr = int(parts[0], 16)
                end_addr = start_addr + int(parts[1], 16)
                print(f".{section_prefix}.{name} start: {start_addr:x}, end: {end_addr:x}, len: {(end_addr - start_addr):x}")
                return start_addr, end_addr


def get_section_start(name):
    """Returns start addr of the section (like *(.text*))"""
    with open(f"build/Release/{prog_name}.map") as f:
        prev_line = ""
        while True:
            line = next(f)
            if line.strip() == name:
                break
            prev_line = line
    start_addr = prev_line.strip().split()[0]
    start_addr = int(start_addr, 16)
    return start_addr


def align(addr, val=4):
    return int(np.ceil(addr / val) * val)


def get_fn_registers(fn_name):
    output = subprocess.check_output(shlex.split(f'arm-none-eabi-objdump -S -z --disassemble={fn_name} build/Release/x6100_mcu.elf'))
    collect = False
    registers = {}
    reg_map = {
        "sl": "r10",
        "fp": "r11",
        "ip": "r12",
        "sp": "r13",
        "lr": "r14",
        "pc": "r15",
    }
    for line in output.splitlines():
        line = line.decode()
        if line.startswith("Disassembly of section"):
            continue
        if not line.strip():
            continue

        if line.endswith(f'<{fn_name}>:'):
            collect = True
            continue
        if not collect:
            continue

        if "\t.word\t" in line:
            continue
        if "\t.short\t" in line:
            continue
        if "\t.byte\t" in line:
            continue

        parts = line.split("\t")
        if parts[2] == "bl" or (parts[2] == "b.w" and "+" not in parts[3]):
            print("!!!bl call:", line)
            sub_fn_name = parts[3].split()[1].strip('<>')
            sub_registers = get_fn_registers(sub_fn_name)
            registers.update({k: f"{sub_fn_name} {v}" for k, v in sub_registers.items()})
            continue
        if len(parts) < 4:
            continue
        if parts[2] == 'it' or parts[2] == "itt":
            continue
        if parts[2] == "bx" and parts[3] == "lr":
            continue
        cmd = parts[2]
        cmd_args = parts[3]
        cmd_args = cmd_args.split(',')
        first_arg = cmd_args[0]
        if fn_name in first_arg:
            continue
        if first_arg.startswith('APSR_'):
            continue
        cmd_args = [x.strip("!{}[] ") for x in cmd_args]
        first_arg = cmd_args[0]
        # if cmd == "push":
        #     continue
        #     pushed_registers |= {reg_map.get(x, x) for x in cmd_args} - registers.keys()
        # elif cmd == "stmdb" and first_arg == "sp":
        #     pushed_registers |= {reg_map.get(x, x) for x in cmd_args[1:]} - registers.keys()
        #     continue
        first_arg = reg_map.get(first_arg, first_arg)
        if (first_arg not in registers) and (first_arg != "sp"):
            registers[first_arg] = line
    return registers


def print_used_registers(fn_name="compress"):
    registers = get_fn_registers(fn_name)
    registers = sorted(registers.items(), key=lambda x: x[0])
    reg_map = {
        "sl": "r10",
        "fp": "r11",
        "ip": "r12",
        "sp": "r13",
        "lr": "r14",
        "pc": "r15",
    }
    for k, l in registers:
        print(k, l)


class InjectFunction:
    def __init__(self, name, inject_addr):
        self.name = name
        self.inject_addr = inject_addr
        self.start_addr, self.end_addr = get_block_start_end(name)
        self.wrapper_len = 0
        self.insert_code = b""

    def setup_wrapper_len(self, asm_o_file):
        # Maybe switch to arm-none-eabi-size -A  asm/helper.o
        self.wrapper_len = align(len(get_patched_section(asm_o_file, f".{self.name}_wrapper")))

    def print_used_registers(self):
        print(f"Registers for {self.name}")
        print_used_registers(fn_name=self.name)
        print("")

    def setup_insert_code(self, asm_elf):
        self.insert_code = get_patched_section(asm_elf, f".insert_to_{self.name}")
        assert len(self.insert_code) == 4, f"{self.name} insert is not 4 bytes"


class InjectAsm(InjectFunction):
    def __init__(self, name, inject_addr, desired_len=4):
        self.name = name
        self.inject_addr = inject_addr
        self.insert_code = b""
        self.start_addr = None
        self.wrapper_len = 0
        self.end_addr = 0
        self.desired_len = desired_len

    def setup_wrapper_len(self, asm_o_file):
        pass

    def print_used_registers(self):
        pass

    def setup_insert_code(self, asm_elf):
        self.insert_code = get_patched_section(asm_elf, f".insert_to_{self.name}")
        assert len(self.insert_code) == self.desired_len, f"{self.name} insert is not {self.desired_len} bytes"


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
            if (fn.wrapper_len):
                print(f"Wrapper len: {fn.wrapper_len}")
            fn.print_used_registers()
            self.sections[f"insert_to_{fn.name}"] = fn.inject_addr
            if fn.wrapper_len:
                self.sections[f"{fn.name}_wrapper"] = orig_fw_size + flash_offset + accumulated_size
            if fn.start_addr is not None:
                self.sections[fn.name] = fn.start_addr
            accumulated_size += fn.wrapper_len
            self.new_code_end = max(self.new_code_end, fn.end_addr)

    def setup_insert_code(self, asm_elf):
        for fn in self.functions:
            fn.setup_insert_code(asm_elf)

    def copy_code(self, src: bytes, dst: bytearray):
        # Copy all from *(.text*) to main function
        start = get_section_start("*(.text*)")
        # end, _ = get_block_start_end("Reset_Handler", "text")
        end = self.rodata_start
        start -= self.flash_offset
        end -= self.flash_offset
        dst[start: end] = src[start: end]
        # for fn in self.functions:
        #     start = fn.start_addr - self.flash_offset
        #     end = fn.end_addr - self.flash_offset
        #     dst[start: end] = src[start: end]

    def copy_rodata(self, src: bytes, dst: bytearray):
        start = self.rodata_start - self.flash_offset
        end = self.rodata_end - self.flash_offset
        dst[start: end] = src[start: end]

    def copy_wrappers(self, asm_elf, dst: bytearray):
        for fn in self.functions:
            if fn.wrapper_len == 0:
                continue
            sec_name = f"{fn.name}_wrapper"
            start = self.sections[sec_name] - self.flash_offset
            code = get_patched_section(asm_elf, f".{sec_name}")
            end = start + len(code)
            dst[start: end] = code

    def insert_jumps(self, dst: bytearray):
        for fn in self.functions:
            from_sec_name = f"insert_to_{fn.name}"
            from_offset = self.sections[from_sec_name] - self.flash_offset
            # Copy instructions
            code = fn.insert_code
            dst[from_offset: from_offset + len(code)] = code


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

    # check from ghidra
    flash_offset = 0x08020000

    # Compile helpers
    asm = "asm/helper.s"
    o_file = "asm/helper.o"
    compile_patch_helper(asm, o_file, date=patchset["date"])

    # get helpers wrappers size
    helper_sizes = get_patch_helper_sizes(o_file)
    total_wrapper_len = sum(math.ceil(x/4) * 4 for x in helper_sizes["wrappers"].values())

    make_stm32f_ld_file(patchset, end_orig_and_wrappers=flash_offset + len(orig_code) + total_wrapper_len)
    compile_c_binaries(date=patchset["date"])

    with open(f"build/Release/{prog_name}.bin", "rb") as f:
        patched_code = f.read()

    # get addresses of binary
    symbols = get_elf_addresses(f"build/Release/{prog_name}.elf")

    rodata_start = symbols["_srodata"]
    rodata_end = symbols["_erodata"]

    inserts = [
        Insert("init_data"),  # fill ram area with zeros
        Insert("configure"),  # configure state at start of DMA handler
        Insert("dma_end"),  # code for end of the DMA handler
        Insert("remove_iq_offset"),  # Remove IQ offset from incoming data
        Insert("if_shift_rx"),  # Apply IF shift

        Insert("if_shift_tx"),  # Handle IF shift on TX
        Insert("compress"),  # compress, limit TX signal
        Insert("am_modulation"),  # soft limit AM signal and modulate

        Insert("fm_modulate"),  # fm modulation prepare
        Insert("tx_amp"),  # amp IQ according to configured TX power
        Insert("tx_coeff_calc"),  # update coefficients for IQ on TX power change
        Insert("fm_demodulate"),  # Demodulate FM

        Insert("am_fm_rx_process"),  # process AM/FM rx (sql, dc blocker)
        Insert("anf_update"),  # update notch filter params
        Insert("nr_apply"),  # noise reduction
        Insert("nb_apply"),  # noise blanker
        Insert("copy_flow"),  # copy data samples to flow with changes
        Insert("process_i2c_cmd"),  # handle i2c commands

        Insert("vox_update"),  # Update in and out audio for VOX
        Insert("vox_restore_audio_input"),  # Restore audio input cfg on stop tx

        Insert("aic_setup_adc_dc_blocker"),  # Setup AIC3204 input dc blocker
    ]

    asms = [
        Insert("skip_am_mult"),
        Insert("skip_oem_nr"),
        Insert("skip_oem_nr_postprocess", desired_len=6),

        Insert("ssb_iq_filter1", desired_len=12),
        Insert("ssb_iq_filter2", desired_len=8),

        Insert("main_board_output_lvl", desired_len=2),
    ]

    # Check len of inserts
    for ins in inserts + asms:
        size = helper_sizes["inserts"][f".insert_to_{ins.name}"]
        assert size == ins.desired_len, ins.name

        print(f"Registers for {ins.name}")
        print_used_registers(fn_name=ins.name)
        print("")

    # prepare LD script for helpers
    make_asm_ld_file(patchset,
        inserts=inserts,
        asms=asms,
        oem_fw_end=flash_offset + len(orig_code),
        new_addresses=symbols,
    )

    helper_output = "asm/helper"
    link_patch_helper(o_file, helper_output)

    with open(helper_output + ".bin", "rb") as f:
        helper_code = f.read()

    dst = bytearray(rodata_end - flash_offset)

    # Copy original code
    dst[:len(orig_code)] = orig_code

    # Copy inserts
    for ins in inserts + asms:
        start = patchset[ins.name] - flash_offset
        end = start + ins.desired_len
        dst[start: end] = helper_code[start: end]

    # Copy wrappers
    start = len(orig_code)
    end = start + total_wrapper_len
    dst[start: end] = helper_code[start: end]

    # Copy new_blocks
    start = end
    end = rodata_end
    dst[start: end] = patched_code[start: end]

    # import pdb; pdb.set_trace()

    # functions = InjectFunctions([
    #     InjectFunction("init_data", patchset["init_data"]),  # fill ram area with zeros
    #     InjectFunction("configure", patchset["configure"]),  # configure state at start of DMA handler
    #     InjectFunction("dma_end", patchset["dma_end"]),  # code for end of the DMA handler
    #     InjectFunction("remove_iq_offset", patchset["remove_iq_offset"]),  # Remove IQ offset from incoming data
    #     InjectFunction("if_shift_rx", patchset["if_shift_rx"]),  # Apply IF shift

    #     InjectFunction("if_shift_tx", patchset["if_shift_tx"]),  # Handle IF shift on TX
    #     InjectFunction("compress", patchset["compress"]),  # compress, limit TX signal
    #     InjectFunction("am_modulation", patchset["am_modulation"]),  # soft limit AM signal and modulate

    #     InjectFunction("fm_modulate", patchset["fm_modulate"]),  # fm modulation prepare
    #     InjectFunction("tx_amp", patchset["tx_amp"]),  # amp IQ according to configured TX power
    #     InjectFunction("tx_coeff_calc", patchset["tx_coeff_calc"]),  # update coefficients for IQ on TX power change
    #     InjectFunction("fm_demodulate", patchset["fm_demodulate"]),  # Demodulate FM

    #     InjectFunction("am_fm_rx_process", patchset["am_fm_rx_process"]),  # process AM/FM rx (sql, dc blocker)
    #     InjectFunction("anf_update", patchset["anf_update"]),  # update notch filter params
    #     InjectFunction("nr_apply", patchset["noise_reduction"]),  # noise reduction
    #     InjectFunction("nb_apply", patchset["noise_blanker"]),  # noise blanker
    #     InjectFunction("copy_flow", patchset["copy_flow"]),  # copy data samples to flow with changes
    #     InjectFunction("process_i2c_cmd", patchset["process_i2c_cmd"]),  # handle i2c commands

    #     InjectFunction("vox_update", patchset["vox_update"]),  # Update in and out audio for VOX
    #     InjectFunction("vox_restore_audio_input", patchset["vox_restore_audio_input"]),  # Restore audio input cfg on stop tx

    #     InjectFunction("aic_setup_adc_dc_blocker", patchset["aic_setup_adc_dc_blocker"]),  # Setup AIC3204 input dc blocker

    #     InjectAsm("skip_am_mult", patchset["skip_am_mult"]),
    #     InjectAsm("skip_oem_nr", patchset["skip_oem_nr"]),
    #     InjectAsm("skip_oem_nr_postprocess", patchset["skip_oem_nr_postprocess"], desired_len=6),

    #     InjectAsm("ssb_iq_filter1", patchset["ssb_iq_filter1"], desired_len=12),
    #     InjectAsm("ssb_iq_filter2", patchset["ssb_iq_filter2"], desired_len=8),
    # ], asm_o_file=o_file, flash_offset=flash_offset, orig_fw_size=len(orig_code), rodata_start=rodata_start, rodata_end=rodata_end)

    # elf = "asm/helper.elf"
    # link_patch_helper(o_file, elf, flash_offset, **functions.sections)

    # functions.setup_insert_code(elf)

    # dst = bytearray(functions.new_code_end - flash_offset)
    # # Copy original code
    # dst[:len(orig_code)] = orig_code

    # # copy new_blocks
    # functions.copy_code(patched_code, dst)
    # # copy rodata
    # functions.copy_rodata(patched_code, dst)

    # # copy wrappers
    # functions.copy_wrappers(elf, dst)

    # # insert jumps
    # functions.insert_jumps(dst)

    # update stack pointers
    # for stack_p in (stack_p_0, stack_p_1):
    #     offset = stack_p - flash_offset
    #     dst[offset: offset + 4] = np.uint32(stack_new_p).tobytes()

    # Update filters
    for name, filter_params in patchset["filter_data"].items():
        print("Update filters for", name)
        update_filters(dst,
            flash_offset,
            filter_params['coeffs_addr'],
            filter_params['file'], filter_params['numTaps']
        )

    # Swap calling filters filters to solve CW AGC
    # Was lpf1, lpf2, agc, hpf1, hpf1
    # New lpf1, hpf1, agc, lpf2, hpf2
    addr1 = 0x08024bc2 - flash_offset
    addr2 = 0x08024c88 - flash_offset
    tmp = dst[addr1: addr1 + 4]
    dst[addr1: addr1 + 4] = dst[addr2: addr2 + 4]
    dst[addr2: addr2 + 4] = tmp


    # Replace IIR filters coeffs
    lpf_addr = 0x0805be50 - flash_offset
    hpf_addr = 0x0803dbe0 - flash_offset
    with open("filters_lpf.data", "rb") as f:
        data = f.read()
        l = len(data)
        dst[lpf_addr:lpf_addr+l] = data
        print(f"lpf len: {l}")
    with open("filters_hpf.data", "rb") as f:
        data = f.read()
        l = len(data)
        dst[hpf_addr:hpf_addr+l] = data


    ver = "r11"
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
