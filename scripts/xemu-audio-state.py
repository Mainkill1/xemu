"""Read QEMU audio ownership from an attached, paused xemu with matching DWARF.

Run with GDB's `source` command through the maintained runner external recipe.
No inferior calls, memory writes, breakpoints, signal changes or PCM mutation.
The report is an instantaneous diagnostic, never a performance measurement.
"""
import hashlib
import json
from pathlib import Path
import gdb


def optional_string(pointer, field_name, max_length=256):
    try:
        address = int(pointer)
        if not address:
            return None
        inferior = gdb.selected_inferior()
        data = bytearray()
        for offset in range(max_length):
            byte = bytes(inferior.read_memory(address + offset, 1))
            if byte == b'\0':
                return data.decode('utf-8', errors='replace')
            data.extend(byte)
    except Exception as error:
        raise ValueError(f'{field_name}: cannot read string: {error}') from error
    raise ValueError(f'{field_name}: no NUL within {max_length} bytes')


def executable_hash(path):
    digest = hashlib.sha256()
    try:
        with path.open('rb') as executable:
            while chunk := executable.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise ValueError(f'{path}: cannot read executable identity: {error}') from error
    return digest.hexdigest()


def check_audio_fields():
    required = {
        'AudioBackend': ['drv', 'dev', 'period_ticks', 'timer_running',
                         'vm_running', 'hw_head_out'],
        'HWVoiceOut': ['enabled', 'info', 'ts_helper', 'sw_head', 'entries'],
        'SWVoiceOut': ['name', 'active', 'empty', 'info',
                       'total_hw_samples_mixed', 'vol', 'entries'],
    }
    for type_name, names in required.items():
        try:
            fields = {field.name for field in gdb.lookup_type(type_name).fields()}
        except Exception as error:
            raise ValueError(f'{type_name}: missing matching audio DWARF: {error}') from error
        missing = sorted(set(names) - fields)
        if missing:
            raise ValueError(f'{type_name}: unsupported audio layout; missing {missing}')
    return required


def write_report(path, report):
    with path.open('x') as output:
        output.write(json.dumps(report, indent=2) + '\n')


def hardware_voices(backend):
    records = []
    voice = backend['hw_head_out']['lh_first']
    seen = set()
    while int(voice):
        address = int(voice)
        if address in seen or len(records) >= 32:
            raise ValueError('Unexpected hardware voice list cycle/length')
        seen.add(address)
        hardware = voice.dereference()
        software = hardware['sw_head']['lh_first']
        software_seen = set()
        software_records = []
        while int(software):
            address = int(software)
            if address in software_seen or len(software_records) >= 64:
                raise ValueError('Unexpected software voice list cycle/length')
            software_seen.add(address)
            item = software.dereference()
            software_records.append(dict(name=optional_string(item['name'], 'SWVoiceOut.name'),
                                         active=bool(item['active']), empty=bool(item['empty']),
                                         frequency=int(item['info']['freq']),
                                         channels=int(item['info']['nchannels']),
                                         totalHardwareSamplesMixed=int(item['total_hw_samples_mixed']),
                                         mute=bool(item['vol']['mute'])))
            software = item['entries']['le_next']
        records.append(dict(enabled=bool(hardware['enabled']),
                            frequency=int(hardware['info']['freq']),
                            channels=int(hardware['info']['nchannels']),
                            hardwareTimestampHelper=int(hardware['ts_helper']),
                            softwareVoices=software_records))
        voice = hardware['entries']['le_next']
    return records


try:
    pid = gdb.selected_inferior().pid
    if pid <= 0:
        raise ValueError('No live inferior attached; inspect GDB stderr for the attach failure')
    executable = Path('/proc') / str(pid) / 'exe'
    backend_pointer = gdb.parse_and_eval('default_audio_be')
    report = dict(schema=1, tool='xemu-audio-state.py',
                  fieldChecks=check_audio_fields(),
                  pid=pid, executableSha256=executable_hash(executable),
                  defaultBackendPresent=bool(int(backend_pointer)),
                  limitation='Paused instantaneous ownership/activity snapshot; no PCM '
                             'audibility, timing, playback coverage or performance claim')
    if int(backend_pointer):
        backend = backend_pointer.dereference()
        report.update(driverName=optional_string(backend['drv'].dereference()['name'], 'AudioBackend.drv.name'),
                      driverEnum=str(backend['dev'].dereference()['driver']),
                      deviceId=optional_string(backend['dev'].dereference()['id'], 'AudioBackend.dev.id'),
                      timerPeriodTicks=int(backend['period_ticks']),
                      timerRunning=bool(backend['timer_running']),
                      vmRunning=bool(backend['vm_running']),
                      hardwareOutputVoices=hardware_voices(backend))
    path = Path('audio-state.json')
    write_report(path, report)
    print('XEMU_AUDIO_STATE ' + json.dumps(report))
except Exception as error:
    print('XEMU_AUDIO_STATE_ERROR ' + str(error))
    gdb.execute('quit 1')
