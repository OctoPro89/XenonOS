#pragma once

#include <drivers/audio/intel/intel_hda.h>

/**
 * @note Called after the HDA controller has enumerated its codecs and routes
 */
b8 intel_hda_audio_register(intel_hda_driver_t* driver);