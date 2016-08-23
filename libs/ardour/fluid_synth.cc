/*
 * Copyright (C) 2016 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "pbd/failed_constructor.h"
#include "ardour/fluid_synth.h"

using namespace ARDOUR;

FluidSynth::FluidSynth (float samplerate, int polyphony)
	: _settings (0)
	, _synth (0)
	, _f_midi_event (0)
{
	_settings = new_fluid_settings ();

	if (!_settings) {
		throw failed_constructor ();
	}

	_f_midi_event = new_fluid_midi_event ();

	if (!_f_midi_event) {
		throw failed_constructor ();
	}

	fluid_settings_setnum (_settings, "synth.sample-rate", samplerate);
	fluid_settings_setint (_settings, "synth.parallel-render", 1);
	fluid_settings_setint (_settings, "synth.threadsafe-api", 0);

	_synth = new_fluid_synth (_settings);

	fluid_synth_set_gain (_synth, 1.0f);
	fluid_synth_set_polyphony (_synth, polyphony);
	fluid_synth_set_sample_rate (_synth, (float)samplerate);
}

FluidSynth::~FluidSynth ()
{
	delete_fluid_synth (_synth);
	delete_fluid_settings (_settings);
	delete_fluid_midi_event (_f_midi_event);
}

bool
FluidSynth::load_sf2 (const std::string& fn)
{
	 _synth_id = fluid_synth_sfload (_synth, fn.c_str (), 1);
	 if (_synth_id == FLUID_FAILED) {
		 return false;
	 }

	 fluid_sfont_t* const sfont = fluid_synth_get_sfont_by_id (_synth, _synth_id);
	 if (!sfont) {
		 return false;
	 }

	 size_t count;
	 fluid_preset_t preset;

	 sfont->iteration_start (sfont);
	 for (count = 0; sfont->iteration_next (sfont, &preset) != 0; ++count) {
		 _presets.push_back (BankProgram (
					 preset.get_name (&preset),
					 preset.get_banknum (&preset),
					 preset.get_num (&preset)));
	 }

	 if (count == 0) {
		 return false;
	 }

	 select_program (0, 0);

	 return true;
}

bool
FluidSynth::select_program (uint32_t pgm, uint8_t chan)
{
	if (pgm >= _presets.size ()) {
		return false;
	}
	const BankProgram& bp = _presets[pgm];
	return FLUID_OK == fluid_synth_program_select (_synth, chan, _synth_id, bp.bank, bp.program);
}

void
FluidSynth::panic ()
{
	fluid_synth_all_notes_off (_synth, -1);
	fluid_synth_all_sounds_off (_synth, -1);
}

bool
FluidSynth::synth (float* left, float* right, uint32_t n_samples)
{
	return FLUID_OK == fluid_synth_write_float (_synth, n_samples, left, 0, 1, right, 0, 1);
}

bool
FluidSynth::midi_event (uint8_t const* const data, size_t len)
{
	if (len > 3) {
		return false;
	}
	fluid_midi_event_set_type (_f_midi_event, data[0] & 0xf0);
	fluid_midi_event_set_channel (_f_midi_event, data[0] & 0x0f);
	if (len > 1) {
		fluid_midi_event_set_key (_f_midi_event, data[1]);
	}
	if (len > 2) {
		fluid_midi_event_set_value (_f_midi_event, data[2]);
	}
	return FLUID_OK == fluid_synth_handle_midi_event (_synth, _f_midi_event);
}