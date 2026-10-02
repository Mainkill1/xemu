"""Read QEMU audio ownership from an attached, paused xemu with matching DWARF.

Run with GDB's `source` command through the maintained runner external recipe.
No inferior calls, memory writes, breakpoints, signal changes or PCM mutation.
The report is an instantaneous diagnostic, never a performance measurement.
"""
import hashlib
import json
from pathlib import Path
import gdb


def optional_string(pointer):
    return pointer.string(errors='replace') if int(pointer) else None


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
            software_records.append(dict(name=optional_string(item['name']),
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
    report = dict(pid=pid, executableSha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  defaultBackendPresent=bool(int(backend_pointer)),
                  limitation='Paused instantaneous ownership/activity snapshot; no PCM '
                             'audibility, timing, playback coverage or performance claim')
    if int(backend_pointer):
        backend = backend_pointer.dereference()
        report.update(driverName=optional_string(backend['drv'].dereference()['name']),
                      driverEnum=str(backend['dev'].dereference()['driver']),
                      deviceId=optional_string(backend['dev'].dereference()['id']),
                      timerPeriodTicks=int(backend['period_ticks']),
                      timerRunning=bool(backend['timer_running']),
                      vmRunning=bool(backend['vm_running']),
                      hardwareOutputVoices=hardware_voices(backend))
    path = Path('audio-state.json')
    if path.exists():
        raise ValueError('Refusing an existing audio-state report')
    path.write_text(json.dumps(report, indent=2) + '\n')
    print('XEMU_AUDIO_STATE ' + json.dumps(report))
except Exception as error:
    print('XEMU_AUDIO_STATE_ERROR ' + str(error))
    gdb.execute('quit 1')
