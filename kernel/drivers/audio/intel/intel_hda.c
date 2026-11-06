#include <drivers/audio/intel/intel_hda.h>
#include <hw/mmio.h>
#include <memory/allocators/dma_allocator.h>
#include <memory/heap.h>
#include <memory/paging.h>
#include <task.h>
#include <time/time.h>
#include <xlibc/stdio.h>
#include <xlibc/string.h>

// TODO: cleanup

static void intel_hda_driver_dump_amp_state(intel_hda_driver_t* driver, u8 codec_addr, u8 nid, b8 output_amp, u8 index);
static b8 intel_hda_get_parameter(intel_hda_driver_t* driver, u8 codec_addr, u8 node_id, u8 parameter, u32* response);
static const char* intel_hda_pin_device_name(u8 device);
static void intel_hda_driver_find_output_paths(intel_hda_driver_t* driver, u8 codec_addr, u8 root_pin, u8 current_nid, u8* reverse_nodes, u8* reverse_edge_indices, u8 depth);

static b8 intel_hda_driver_set_amp_0db(intel_hda_driver_t* driver, u8 codec_addr, intel_hda_widget_t* widget, b8 output_amp, u8 connection_index) {
	if (!driver || !widget || !widget->valid) {
		return false;
	}

	u32 caps;

	if (output_amp) {
		if (!widget->has_output_amp_caps) {
			return true;
		}

		caps = widget->output_amp_caps;
	} else {
		if (!widget->has_input_amp_caps) {
			return true;
		}

		caps = widget->input_amp_caps;

		if (connection_index > 0x0F) {
			printf("[INTEL HDA]: Amp connection index exceeds verb limits.\n");
			return false;
		}
	}

	// the Offset field indicates which gain code represents 0 dB; do not assume gain code zero means 0 dB
	u8 gain = caps & 0x7F;

	/*
	 * Set Amplifier Gain/Mute:
	 *
	 * Bit 15: Output amplifier
	 * Bit 14: Input amplifier
	 * Bit 13: Left channel
	 * Bit 12: Right channel
	 * Bits 11:8: Connection index
	 * Bit 7: Mute
	 * Bits 6:0: Gain
	 */
	u16 payload = 0;

	if (output_amp) {
		payload |= (1u << 15);
	} else {
		payload |= (1u << 14);
		payload |= (u16)(connection_index & 0x0F) << 8;
	}

	// both stereo channels
	payload |= (1u << 13);
	payload |= (1u << 12);

	// mute is intentionally clear
	payload |= gain;

	u32 response = 0;

	if (!intel_hda_driver_send_verb4(driver, codec_addr, widget->nid, HDA_VERB_SET_AMP_GAIN_MUTE, payload, &response)) {
		printf("[INTEL HDA]: Failed to configure amplifier NID %d.\n", (int)widget->nid);
		return false;
	}

	printf("[INTEL HDA]: Configured %s amplifier on NID %d, gain %d, index %d.\n", output_amp ? "output" : "input", (int)widget->nid, (int)gain, (int)connection_index);
	return true;
}

static b8 intel_hda_driver_configure_path_connection(intel_hda_driver_t* driver, u8 codec_addr, intel_hda_output_path_t* path, u8 path_index) {
	if (!driver || !path || path_index == 0 || path_index >= path->depth) {
		return false;
	}

	u8 nid = path->nodes[path_index];

	u8 source_nid = path->nodes[path_index - 1];

	intel_hda_widget_t* widget = &driver->widgets[codec_addr][nid];

	if (!widget->valid) {
		return false;
	}

	// verify stored connection index still refers to the expected source
	u8 index = path->source_index[path_index];

	if (!widget->connections || index >= widget->num_connections || widget->connections[index] != source_nid) {
		printf("[INTEL HDA]: Invalid cached connection NID %d -> NID %d.\n", (int)nid, (int)source_nid);
		return false;
	}

	// SELECTOR WIDGET: a selector must choose the appropriate input
	if (widget->type == HDA_WIDGET_TYPE_AUDIO_SELECTOR) {
		if (widget->num_connections > 1) {
			u32 response = 0;

			if (!intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_SET_CONNECTION_SELECT_CONTROL, index, &response)) {
				return false;
			}

			printf("[INTEL HDA]: Selector NID %d selected connection %d.\n", (int)nid, (int)index);
		}

		// configure the selector's selected input amplifier, if it has one
		if (widget->has_input_amp_caps) {
			if (!intel_hda_driver_set_amp_0db(driver, codec_addr, widget, false, index)) {
				return false;
			}
		}
	}

	// MIXER WIDGET: do NOT send Set Connection Select here; the mixer combines its inputs, unmute the specific input used by the route
	else if (widget->type == HDA_WIDGET_TYPE_AUDIO_MIXER) {
		if (widget->has_input_amp_caps) {
			if (!intel_hda_driver_set_amp_0db(driver, codec_addr, widget, false, index)) {
				return false;
			}
		}
	}

	// PIN COMPLEX: A pin with multiple incoming connections can select the signal feeding its external output
	else if (widget->type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
		if (widget->num_connections > 1) {
			u32 response = 0;

			if (!intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_SET_CONNECTION_SELECT_CONTROL, index, &response)) {
				return false;
			}

			printf("[INTEL HDA]: Pin NID %d selected connection %d.\n", (int)nid, (int)index);
		}
	}

	return true;
}

static b8 intel_hda_driver_ensure_function_group_d0(intel_hda_driver_t* driver, u8 codec_addr, u8 fg_nid) {
	u32 supported = 0;

	if (!intel_hda_get_parameter(driver, codec_addr, fg_nid, HDA_PARAMETER_SUPPORTED_POWER_STATES, &supported)) {
		return false;
	}

	if (!(supported & 1u)) {
		printf("[INTEL HDA]: Function Group NID %d does not support D0.\n", (int)fg_nid);
		return false;
	}

	u32 state = 0;

	if (!intel_hda_driver_send_verb(driver, codec_addr, fg_nid, HDA_VERB_GET_POWER_STATE, 0, &state)) {
		return false;
	}

	u8 setting = state & 0x0F;
	u8 actual = (state >> 4) & 0x0F;

	if (setting == 0 && actual == 0) {
		return true;
	}

	u32 response = 0;

	if (!intel_hda_driver_send_verb(driver, codec_addr, fg_nid, HDA_VERB_SET_POWER_STATE, 0, &response)) {
		return false;
	}

	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		if (!intel_hda_driver_send_verb(driver, codec_addr, fg_nid, HDA_VERB_GET_POWER_STATE, 0, &state)) {
			return false;
		}

		setting = state & 0x0F;
		actual = (state >> 4) & 0x0F;

		if (setting == 0 && actual == 0) {
			printf("[INTEL HDA]: Function Group NID %d is now in D0.\n", (int)fg_nid);
			return true;
		}

		msleep(1);
	}

	printf("[INTEL HDA]: Function Group NID %d failed to enter D0.\n", (int)fg_nid);
	return false;
}

static b8 intel_hda_driver_stop_output_stream(intel_hda_driver_t* driver) {
	if (!driver || !driver->regs) {
		return false;
	}

	u32 index = driver->playback_stream_index;

	vaddr_t sd = (vaddr_t)driver->regs + HDA_REG_STREAM_DESC_BASE + (index * HDA_STREAM_DESC_STRIDE);

	u8 ctl = mmio_read8(sd);

	// clear RUN, preserving the other control bits
	mmio_write8(sd, ctl & ~HDA_SD_CTL_RUN);

	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		if (!(mmio_read8(sd) & HDA_SD_CTL_RUN)) {
			driver->playback_running = false;

			printf("[INTEL HDA]: Playback stream stopped.\n");
			return true;
		}

		msleep(1);
	}

	printf("[INTEL HDA]: Timed out stopping playback stream.\n");
	return false;
}

static b8 intel_hda_driver_start_output_stream(intel_hda_driver_t* driver) {
	if (!driver || !driver->regs || !driver->playback_prepared) {
		printf("[INTEL HDA]: Playback stream is not prepared.\n");
		return false;
	}

	if (driver->playback_running) {
		printf("[INTEL HDA]: Playback stream is already running.\n");
		return false;
	}

	u32 index = driver->playback_stream_index;

	vaddr_t sd = (vaddr_t)driver->regs + HDA_REG_STREAM_DESC_BASE + (index * HDA_STREAM_DESC_STRIDE);

	printf("[INTEL HDA]: Starting playback stream...\n");

	// verify descriptor configuration
	u32 cbl = mmio_read32(sd + 0x08);
	u16 lvi = mmio_read16(sd + 0x0C);
	u16 format = mmio_read16(sd + 0x12);

	u8 ctl0 = mmio_read8(sd);
	u8 ctl2 = mmio_read8(sd + 2);

	printf("[INTEL HDA]: CBL: %d | LVI: %d | Format: 0x%04x\n", (int)cbl, (int)lvi, (unsigned int)format);

	if (cbl != 8192 || lvi != 1 || format != HDA_PLAYBACK_FORMAT) {
		printf("[INTEL HDA]: Invalid playback stream configuration.\n");
		return false;
	}

	if ((ctl0 & HDA_SD_CTL_RUN) || (ctl0 & HDA_SD_CTL_SRST)) {
		printf("[INTEL HDA]: Stream is running or in reset.\n");
		return false;
	}

	u8 stream_tag = (ctl2 >> 4) & 0x0F;

	if (stream_tag != driver->playback_stream_tag) {
		printf("[INTEL HDA]: Stream tag mismatch!\n");
		return false;
	}

	// clear pending status flags; BCIS, FIFOE and DESE are write-one-to-clear, FIFORDY is read-only, so don't write it
	mmio_write8(sd + 3, HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE);

	// start the DMA engine, preserve the other control bits
	ctl0 = mmio_read8(sd);

	mmio_write8(sd, ctl0 | HDA_SD_CTL_RUN);

	barrier_io_write();

	printf("[INTEL HDA]: RUN bit asserted.\n");

	// wait for the RUN bit and FIFO Ready; for output streams, FIFORDY means the controller has fetched enough audio data to maintain the stream on the HDA link
	b8 fifo_ready = false;

	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		ctl0 = mmio_read8(sd);
		u8 status = mmio_read8(sd + 3);

		if (status & (HDA_SD_STS_FIFOE | HDA_SD_STS_DESE)) {
			printf("[INTEL HDA]: Stream error while starting: 0x%02x\n", status);

			mmio_write8(sd + 3, status & (HDA_SD_STS_FIFOE | HDA_SD_STS_DESE));
			intel_hda_driver_stop_output_stream(driver);
			return false;
		}

		if (!(ctl0 & HDA_SD_CTL_RUN)) {
			printf("[INTEL HDA]: Hardware cleared RUN unexpectedly.\n");
			return false;
		}

		if (status & HDA_SD_STS_FIFORDY) {
			fifo_ready = true;
			break;
		}

		msleep(1);
	}

	if (!fifo_ready) {
		printf("[INTEL HDA]: Timed out waiting for FIFO Ready. Status: 0x%02x\n", mmio_read8(sd + 3));

		intel_hda_driver_stop_output_stream(driver);

		return false;
	}

	printf("[INTEL HDA]: FIFO Ready!\n");

	// verify DMA progress using LPIB; LPIB is the current DMA position inside the cyclic buffer
	u32 initial_lpib = mmio_read32(sd + 0x04);

	printf("[INTEL HDA]: Initial LPIB: %d\n", (int)initial_lpib);

	b8 dma_progress = false;
	u32 current_lpib = initial_lpib;

	for (u32 i = 0; i < 10; i++) {
		msleep(2);

		u8 status = mmio_read8(sd + 3);

		if (status & (HDA_SD_STS_FIFOE | HDA_SD_STS_DESE)) {
			printf("[INTEL HDA]: Playback error: 0x%02x\n", status);

			mmio_write8(sd + 3, status & (HDA_SD_STS_FIFOE | HDA_SD_STS_DESE));
			intel_hda_driver_stop_output_stream(driver);
			return false;
		}

		if (!(mmio_read8(sd) & HDA_SD_CTL_RUN)) {
			printf("[INTEL HDA]: Stream stopped unexpectedly.\n");
			return false;
		}

		current_lpib = mmio_read32(sd + 0x04);

		if (current_lpib != initial_lpib) {
			dma_progress = true;
			break;
		}
	}

	if (!dma_progress) {
		printf("[INTEL HDA]: LPIB is not advancing! Initial: %d Current: %d\n", (int)initial_lpib, (int)current_lpib);

		intel_hda_driver_stop_output_stream(driver);
		return false;
	}

	driver->playback_running = true;

	printf("[INTEL HDA]: PLAYBACK STREAM RUNNING!\n");
	printf("       Stream index: %d\n", (int)index);
	printf("       Stream tag: %d\n", (int)driver->playback_stream_tag);
	printf("       Format: 48kHz Stereo 16-bit PCM\n");
	printf("       LPIB: %d -> %d\n", (int)initial_lpib, (int)current_lpib);

	return true;
}

static b8 intel_hda_widget_has_connection(intel_hda_driver_t* driver, u8 codec_addr, u8 source_nid, u8 target_nid) {
	if (!driver || !driver->widgets[codec_addr]) {
		return false;
	}

	intel_hda_widget_t* widget = &driver->widgets[codec_addr][source_nid];

	if (!widget->valid || !widget->connections) {
		return false;
	}

	for (u16 i = 0; i < widget->num_connections; i++) {
		if (widget->connections[i] == target_nid) {
			return true;
		}
	}

	return false;
}

static b8 intel_hda_driver_alloc_dma_page(intel_hda_driver_t* driver, vaddr_t* virt, paddr_t* phys, size_t* allocated_size) {
	if (!driver || !virt || !phys || !allocated_size) {
		return false;
	}

	dma_region_t region = dma_alloc(HDA_PLAYBACK_PAGE_SIZE);

	if (!region.virt || region.size < HDA_PLAYBACK_PAGE_SIZE) {
		printf("[INTEL HDA]: Failed to allocate playback DMA page.\n");
		return false;
	}

	// NOTE: this is stronger than HDA's 128-byte requirement
	if ((region.virt & 0xFFF) || (region.phys & 0xFFF)) {
		printf("[INTEL HDA]: Playback DMA page is not page-aligned.\n");
		return false;
	}

	// verify the entire allocation fits in the controller's DMA address width
	if (!driver->supports_64bit_dma &&
		region.phys > 0xFFFFFFFFULL - (HDA_PLAYBACK_PAGE_SIZE - 1)) {
		printf("[INTEL HDA]: Playback DMA page exceeds 32-bit address space.\n");
		return false;
	}

	memset((void*)region.virt, 0, HDA_PLAYBACK_PAGE_SIZE);

	*virt = region.virt;
	*phys = region.phys;
	*allocated_size = region.size;

	return true;
}

static void intel_hda_driver_fill_test_tone(intel_hda_driver_t* driver) {
	u16* buffer0 = (u16*)driver->playback_buffer_virt[0];
	u16* buffer1 = (u16*)driver->playback_buffer_virt[1];

	const u32 sample_rate = 48000;
	const u32 cycles = 19;

	const u32 frames_per_buffer = HDA_PLAYBACK_BUFFER_SIZE / 4;
	const u32 total_frames = frames_per_buffer * HDA_PLAYBACK_BUFFER_COUNT;
	const u32 quarter = total_frames / 4;
	const i32 amplitude = 2048;

	for (u32 frame = 0; frame < total_frames; frame++) {
		u32 phase = (frame * cycles) % total_frames;

		i32 sample;

		if (phase < quarter) {
			sample = (amplitude * (i32)phase) / quarter;
		} else if (phase < quarter * 3) {
			sample = amplitude - (amplitude * (i32)(phase - quarter)) / quarter;
		} else {
			sample = -amplitude + (amplitude * (i32)(phase - quarter * 3)) / quarter;
		}

		u16 encoded = (u16)sample;
		u16* destination;
		if (frame < frames_per_buffer) {
			destination = buffer0 + frame * 2;
		} else {
			destination = buffer1 + (frame - frames_per_buffer) * 2;
		}

		destination[0] = encoded;
		destination[1] = encoded;
	}

	barrier_io_write();
}

static b8 intel_hda_driver_build_playback_bdl(intel_hda_driver_t* driver) {
	if (!driver) {
		return false;
	}

	intel_hda_bdle_t* bdl = (intel_hda_bdle_t*)driver->playback_bdl_virt;

	memset(bdl, 0, 2 * sizeof(intel_hda_bdle_t));

	bdl[0].address = driver->playback_buffer_phys[0];
	bdl[0].length = HDA_PLAYBACK_BUFFER_SIZE;
	bdl[0].flags = 0;

	bdl[1].address = driver->playback_buffer_phys[1];
	bdl[1].length = HDA_PLAYBACK_BUFFER_SIZE;
	bdl[1].flags = 0;

	barrier_io_write();

	return true;
}

static b8 intel_hda_driver_reset_output_stream(intel_hda_driver_t* driver, u8 stream_index) {
	if (!driver || !driver->regs || stream_index >= 32) {
		return false;
	}

	vaddr_t stream_base = (vaddr_t)driver->regs + HDA_REG_STREAM_DESC_BASE + ((u32)stream_index * HDA_STREAM_DESC_STRIDE);

	// make sure RUN is cleared first
	u8 ctl0 = mmio_read8(stream_base);

	if (ctl0 & HDA_SD_CTL_RUN) {
		mmio_write8(stream_base, ctl0 & ~HDA_SD_CTL_RUN);

		b8 stopped = false;
		for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
			if (!(mmio_read8(stream_base) & HDA_SD_CTL_RUN)) {
				stopped = true;
				break;
			}

			msleep(1);
		}

		if (!stopped) {
			printf("[INTEL HDA]: Stream failed to stop.\n");
			return false;
		}
	}

	// assert stream reset
	ctl0 = mmio_read8(stream_base);

	mmio_write8(stream_base, ctl0 | HDA_SD_CTL_SRST);

	b8 reset_asserted = false;
	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		if (mmio_read8(stream_base) & HDA_SD_CTL_SRST) {
			reset_asserted = true;
			break;
		}

		msleep(1);
	}

	if (!reset_asserted) {
		printf("[INTEL HDA]: Stream reset assertion timed out.\n");
		return false;
	}

	// deassert stream reset.
	ctl0 = mmio_read8(stream_base);

	mmio_write8(stream_base, ctl0 & ~HDA_SD_CTL_SRST);

	b8 reset_deasserted = false;
	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		if (!(mmio_read8(stream_base) & HDA_SD_CTL_SRST)) {
			reset_deasserted = true;
			break;
		}

		msleep(1);
	}

	if (!reset_deasserted) {
		printf("[INTEL HDA]: Stream reset deassertion timed out.\n");
		return false;
	}

	printf("[INTEL HDA]: Output stream descriptor %d reset successfully.\n", (int)stream_index);

	return true;
}

static b8 intel_hda_driver_prepare_output_stream(intel_hda_driver_t* driver, u8 codec_addr, u8 dac_nid) {
	if (!driver || !driver->regs) {
		return false;
	}

	if (driver->num_oss == 0) {
		printf("[INTEL HDA]: No output stream descriptors available.\n");
		return false;
	}

	if (driver->playback_prepared) {
		printf("[INTEL HDA]: Playback stream is already prepared.\n");
		return false;
	}

	// the first output stream follows all input streams
	u32 stream_index = driver->num_iss;

	u32 total_streams = driver->num_iss + driver->num_oss + driver->num_bss;
	if (stream_index >= total_streams || stream_index >= 32) {
		return false;
	}

	if (!driver->widgets[codec_addr] || !driver->widgets[codec_addr][dac_nid].valid || driver->widgets[codec_addr][dac_nid].type != HDA_WIDGET_TYPE_AUDIO_OUTPUT) {
		printf("[INTEL HDA]: Invalid playback DAC.\n");
		return false;
	}

	printf("[INTEL HDA]: Preparing playback stream...\n");

	// allocate BDL page
	if (!intel_hda_driver_alloc_dma_page(driver, &driver->playback_bdl_virt, &driver->playback_bdl_phys, &driver->playback_bdl_alloc_size)) {
		return false;
	}

	// allocate PCM buffer 0
	if (!intel_hda_driver_alloc_dma_page(driver, &driver->playback_buffer_virt[0], &driver->playback_buffer_phys[0], &driver->playback_buffer_alloc_size[0])) {
		return false;
	}

	// allocate PCM buffer 1
	if (!intel_hda_driver_alloc_dma_page(driver, &driver->playback_buffer_virt[1], &driver->playback_buffer_phys[1], &driver->playback_buffer_alloc_size[1])) {
		return false;
	}

	driver->playback_dma_allocated = true;

	// check every DMA address for 64-bit compatibility
	if (!driver->supports_64bit_dma) {
		if (driver->playback_bdl_phys > 0xFFFFFFFFULL || driver->playback_buffer_phys[0] > 0xFFFFFFFFULL || driver->playback_buffer_phys[1] > 0xFFFFFFFFULL) {
			printf("[INTEL HDA]: Playback DMA exceeds 32-bit address space.\n");
			return false;
		}
	}

	intel_hda_driver_fill_test_tone(driver);

	if (!intel_hda_driver_build_playback_bdl(driver)) {
		return false;
	}

	// reset the stream descriptor
	if (!intel_hda_driver_reset_output_stream(driver, (u8)stream_index)) {
		return false;
	}

	vaddr_t base = (vaddr_t)driver->regs;
	vaddr_t sd = base + HDA_REG_STREAM_DESC_BASE + (stream_index * HDA_STREAM_DESC_STRIDE);

	// clear pending stream status flags, write-one-to-clear bits
	mmio_write8(sd + 3, HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE);

	// ensure this stream isn't synchronized/stalled through the global SSYNC register
	u32 ssync = mmio_read32(base + HDA_REG_SSYNC);
	mmio_write32(base + HDA_REG_SSYNC, ssync & ~(1u << stream_index));

	// program BDL base address
	if (driver->supports_64bit_dma) {
		mmio_write32(sd + 0x1C, (u32)(driver->playback_bdl_phys >> 32));
	}

	mmio_write32(sd + 0x18, (u32)driver->playback_bdl_phys);

	// configure cyclic buffer length
	mmio_write32(sd + 0x08, HDA_PLAYBACK_BUFFER_SIZE * HDA_PLAYBACK_BUFFER_COUNT);

	// configure last valid descriptor, two entries means the final valid index is 1
	mmio_write16(sd + 0x0C, 1);

	// configure 48 kHz stereo 16-bit PCM
	mmio_write16(sd + 0x12, HDA_PLAYBACK_FORMAT);

	// read FIFO size after programming format
	u16 fifo_size = mmio_read16(sd + 0x10);

	// assign stream tag 1; bits 23:20 of SDnCTL are the stream tag, CTL byte 2 = 0x10.
	driver->playback_stream_tag = HDA_PLAYBACK_STREAM_TAG;

	driver->playback_stream_index = (u8)stream_index;

	u8 ctl2 = mmio_read8(sd + 2);

	ctl2 = (ctl2 & 0x0F) | (HDA_PLAYBACK_STREAM_TAG << 4);

	mmio_write8(sd + 2, ctl2);

	// configure the codec's DAC format
	u32 response = 0;

	if (!intel_hda_driver_send_verb4(driver, codec_addr, dac_nid, HDA_VERB_SET_CONVERTER_FORMAT, HDA_PLAYBACK_FORMAT, &response)) {
		printf("[INTEL HDA]: Failed to configure DAC format.\n");
		return false;
	}

	/*
	 * Associate the DAC with stream 1, channel 0
	 * Payload:
	 * Bits 7:4 = stream tag
	 * Bits 3:0 = channel ID
	 * Stream 1, channel 0 = 0x10.
	 */
	u8 stream_channel = (HDA_PLAYBACK_STREAM_TAG << 4) | 0;

	if (!intel_hda_driver_send_verb(driver, codec_addr, dac_nid, HDA_VERB_SET_CONVERTER_STREAM, stream_channel, &response)) {
		printf("[INTEL HDA]: Failed to assign DAC stream tag.\n");
		return false;
	}

	// store playback information
	driver->playback_codec_addr = codec_addr;
	driver->playback_dac_nid = dac_nid;

	driver->playback_prepared = true;

	printf("[INTEL HDA]: PLAYBACK STREAM PREPARED\n");
	printf("       Stream index: %d\n", (int)stream_index);
	printf("       Stream tag: %d\n", (int)driver->playback_stream_tag);
	printf("       BDL address: 0x%llx\n", (unsigned long long)driver->playback_bdl_phys);
	printf("       CBL: %d bytes\n", HDA_PLAYBACK_BUFFER_SIZE * HDA_PLAYBACK_BUFFER_COUNT);
	printf("       LVI: 1\n");
	printf("       Format: 0x%04x\n", HDA_PLAYBACK_FORMAT);
	printf("       FIFO size: %d bytes\n", (int)fifo_size);
	printf("       Stream RUN: %s\n", (mmio_read8(sd) & HDA_SD_CTL_RUN) ? "Running" : "Stopped");
	printf("       Stream control byte 2: 0x%02x\n", mmio_read8(sd + 2));
	printf("[INTEL HDA]: Playback stream is prepared, but not running.\n");

	return true;
}

static b8 intel_hda_driver_ensure_widget_d0(intel_hda_driver_t* driver, u8 codec_addr, u8 nid) {
	intel_hda_widget_t* widget = &driver->widgets[codec_addr][nid];

	if (!widget->valid) {
		return false;
	}

	// nothing to configure if the widget has no power control
	if (!(widget->capabilities & HDA_WIDGET_CAP_POWER_CTRL)) {
		return true;
	}

	u32 supported_states = 0;

	if (!intel_hda_get_parameter(driver, codec_addr, nid, HDA_PARAMETER_SUPPORTED_POWER_STATES, &supported_states)) {
		return false;
	}

	// D0 must be supported.
	if (!(supported_states & 1u)) {
		printf("[INTEL HDA]: NID %d does not support D0!\n", nid);
		return false;
	}

	u32 state = 0;

	if (!intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_GET_POWER_STATE, 0, &state)) {
		return false;
	}

	u8 setting = state & 0x0F;
	u8 actual = (state >> 4) & 0x0F;

	if (setting == 0 && actual == 0) {
		return true;
	}

	printf("[INTEL HDA]: Powering NID %d to D0...\n", nid);

	u32 response = 0;

	if (!intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_SET_POWER_STATE, 0, &response)) {
		return false;
	}

	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		if (!intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_GET_POWER_STATE, 0, &state)) {
			return false;
		}

		setting = state & 0x0F;
		actual = (state >> 4) & 0x0F;

		if (setting == 0 && actual == 0) {
			printf("[INTEL HDA]: NID %d is now in D0.\n", nid);
			return true;
		}

		msleep(1);
	}

	printf("[INTEL HDA]: Timed out powering NID %d to D0.\n", nid);
	return false;
}

static b8 intel_hda_driver_configure_output_path(intel_hda_driver_t* driver, u8 codec_addr, intel_hda_output_path_t* path, b8 enable_eapd) {
	if (!driver || !path || !path->valid || codec_addr >= HDA_MAX_CODECS ||
		!driver->widgets[codec_addr]) {
		return false;
	}

	if (path->depth < 2 || path->depth > HDA_MAX_PATH_DEPTH) {
		return false;
	}

	// this configurator currently supports analog outputs only
	if (path->pin_device != HDA_PIN_DEVICE_LINE_OUT && path->pin_device != HDA_PIN_DEVICE_SPEAKER && path->pin_device != HDA_PIN_DEVICE_HEADPHONE) {
		printf("[INTEL HDA]: Refusing unsupported digital/non-analog output path " "(Pin %d).\n", (int)path->pin_nid);
		return false;
	}

	if (path->nodes[0] != path->dac_nid ||
		path->nodes[path->depth - 1] != path->pin_nid) {
		return false;
	}

	intel_hda_widget_t* pin = &driver->widgets[codec_addr][path->pin_nid];

	if (!pin->valid || pin->type != HDA_WIDGET_TYPE_PIN_COMPLEX || !pin->has_pin_caps || !(pin->pin_capabilities & HDA_PINCAP_OUTPUT)) {
		return false;
	}

	printf("[INTEL HDA]: Configuring output route: DAC %d -> Pin %d\n", (int)path->dac_nid, (int)path->pin_nid);

	// ensure the Function Group is powered.
	if (!intel_hda_driver_ensure_function_group_d0(driver, codec_addr, path->function_group_nid)) {
		printf("[INTEL HDA]: Failed to power Function Group.\n");
		return false;
	}

	// verify and power every widget in the route
	for (u8 i = 0; i < path->depth; i++) {
		intel_hda_widget_t* widget = &driver->widgets[codec_addr][path->nodes[i]];

		if (!widget->valid || widget->function_group_nid != path->function_group_nid) {
			printf("[INTEL HDA]: Invalid widget in route at index %d.\n", (int)i);
			return false;
		}

		if (i == 0 && widget->type != HDA_WIDGET_TYPE_AUDIO_OUTPUT) {
			return false;
		}

		if (i > 0 && widget->type != HDA_WIDGET_TYPE_AUDIO_MIXER && widget->type != HDA_WIDGET_TYPE_AUDIO_SELECTOR && widget->type != HDA_WIDGET_TYPE_PIN_COMPLEX) {
			return false;
		}

		if (!intel_hda_driver_ensure_widget_d0(driver, codec_addr, widget->nid)) {
			printf("[INTEL HDA]: Failed to power route widget NID %d.\n", (int)widget->nid);
			return false;
		}
	}

	// configure every connection along the route, this selects inputs where necessary and unmutes the relevant mixer input amplifiers
	for (u8 i = 1; i < path->depth; i++) {
		if (!intel_hda_driver_configure_path_connection(driver, codec_addr, path, i)) {
			printf("[INTEL HDA]: Failed configuring connection at NID %d.\n", (int)path->nodes[i]);
			return false;
		}
	}

	// configure output amplifiers along the route; these can exist on DACs, mixers, selectors, and Pin Complex widgets
	for (u8 i = 0; i < path->depth; i++) {
		intel_hda_widget_t* widget = &driver->widgets[codec_addr][path->nodes[i]];
		if (widget->has_output_amp_caps) {
			if (!intel_hda_driver_set_amp_0db(driver, codec_addr, widget, true, 0)) {
				printf("[INTEL HDA]: Failed configuring output amplifier NID " "%d.\n", (int)widget->nid);
				return false;
			}
		}
	}

	// configure the physical output pin
	u32 old_pin_control = 0;
	if (!intel_hda_driver_send_verb(driver, codec_addr, path->pin_nid, HDA_VERB_GET_PIN_WIDGET_CONTROL, 0, &old_pin_control)) {
		return false;
	}

	// preserve analog VRef settings, clear input enable, enable output
	u8 new_pin_control = (old_pin_control & 0x1F) | HDA_PINCTL_OUTPUT_ENABLE;

	// enable the low-impedance headphone amplifier only for a headphone-designated pin
	if (path->pin_device == HDA_PIN_DEVICE_HEADPHONE && (pin->pin_capabilities & HDA_PINCAP_HEADPHONE_DRIVE)) {
		new_pin_control |= HDA_PINCTL_HEADPHONE_ENABLE;
	}

	u32 response = 0;

	if (!intel_hda_driver_send_verb(driver, codec_addr, path->pin_nid, HDA_VERB_SET_PIN_WIDGET_CONTROL, new_pin_control, &response)) {
		return false;
	}

	// configure EAPD where supported and requested
	if (enable_eapd && (pin->pin_capabilities & HDA_PINCAP_EAPD)) {
		u32 old_eapd = 0;

		if (!intel_hda_driver_send_verb(driver, codec_addr, path->pin_nid, HDA_VERB_GET_EAPD_BTL_ENABLE, 0, &old_eapd)) {
			return false;
		}

		// preserve BTL and L/R swap, set EAPD.
		u8 new_eapd = (old_eapd & 0x07) | HDA_EAPD_ENABLE;

		if (!intel_hda_driver_send_verb(driver, codec_addr, path->pin_nid, HDA_VERB_SET_EAPD_BTL_ENABLE, new_eapd, &response)) {
			return false;
		}
	}

	path->configured = true;

	printf("[INTEL HDA]: Output route configured successfully.\n");
	printf("       DAC: NID %d\n", (int)path->dac_nid);
	printf("       Pin: NID %d\n", (int)path->pin_nid);
	printf("       Pin Control: 0x%02x\n", (unsigned int)new_pin_control);

	return true;
}

static intel_hda_output_path_t* intel_hda_driver_find_default_analog_output(
	intel_hda_driver_t* driver, u8 codec_addr) {
	if (!driver || codec_addr >= HDA_MAX_CODECS ||
		!driver->output_paths[codec_addr]) {
		return NULL;
	}

	const u8 priorities[] = { HDA_PIN_DEVICE_LINE_OUT, HDA_PIN_DEVICE_HEADPHONE, HDA_PIN_DEVICE_SPEAKER };

	for (u8 p = 0; p < sizeof(priorities) / sizeof(priorities[0]); p++) {
		for (u16 i = 0; i < driver->output_path_count[codec_addr]; i++) {
			intel_hda_output_path_t* path =
				&driver->output_paths[codec_addr][i];

			if (!path->valid) {
				continue;
			}

			if (path->pin_device == priorities[p]) {
				return path;
			}
		}
	}

	return NULL;
}

static void intel_hda_driver_discover_output_paths(intel_hda_driver_t* driver, u8 codec_addr) {
	if (!driver || codec_addr >= HDA_MAX_CODECS || !driver->widgets[codec_addr] || !driver->output_paths[codec_addr]) {
		return;
	}

	driver->output_path_count[codec_addr] = 0;
	driver->output_paths_truncated[codec_addr] = false;

	printf("[INTEL HDA]: Discovering output routes for codec %d...\n", (int)codec_addr);

	// inspect all possible codec NIDs
	for (u16 nid = 0; nid <= HDA_NODE_ID_MAX; nid++) {
		intel_hda_widget_t* widget = &driver->widgets[codec_addr][nid];

		if (!widget->valid) {
			continue;
		}

		if (widget->type != HDA_WIDGET_TYPE_PIN_COMPLEX) {
			continue;
		}

		if (!widget->has_pin_caps || !widget->has_pin_config) {
			continue;
		}

		// only consider output-capable pins.
		if (!(widget->pin_capabilities & HDA_PINCAP_OUTPUT)) {
			continue;
		}

		/*
		 * Skip explicitly disconnected pins
		 * 0 = Jack
		 * 1 = No physical connection
		 * 2 = Fixed
		 * 3 = Jack and fixed device
		 */
		u8 connectivity = (widget->pin_config >> 30) & 0x03;

		if (connectivity == 1) {
			continue;
		}

		// printf("[INTEL HDA]: Searching pin NID %d...\n", (int)nid);

		u8 reverse_nodes[HDA_MAX_PATH_DEPTH] = {0};

		u8 reverse_edge_indices[HDA_MAX_PATH_DEPTH] = {0};

		intel_hda_driver_find_output_paths(driver, codec_addr, (u8)nid, (u8)nid, reverse_nodes, reverse_edge_indices, 0);
	}

	printf("[INTEL HDA]: Codec %d discovered %d output paths.\n", (int)codec_addr, (int)driver->output_path_count[codec_addr]);

	for (u16 p = 0; p < driver->output_path_count[codec_addr]; p++) {
		intel_hda_output_path_t* path = &driver->output_paths[codec_addr][p];

		/*
		printf("[INTEL HDA]: OUTPUT ROUTE #%d\n", (int)p);
		printf("       Pin: NID %d (%s)\n", (int)path->pin_nid, intel_hda_pin_device_name(path->pin_device));
		printf("       DAC: NID %d\n", (int)path->dac_nid);
		printf("       Association: %d | Sequence: %d\n", (int)path->association, (int)path->sequence);
		printf("       Depth: %d\n", (int)path->depth);
		printf("       Signal path: ");

		for (u8 i = 0; i < path->depth; i++) {
			printf("%d", (int)path->nodes[i]);
			if (i + 1 < path->depth) {
				printf(" -> ");
			}
		}

		printf("\n");

		for (u8 i = 1; i < path->depth; i++) {
			u8 nid = path->nodes[i];
			u8 source_nid = path->nodes[i - 1];
			intel_hda_widget_t* current = &driver->widgets[codec_addr][nid];

			printf("       NID %d receives NID %d through connection index %d", (int)nid, (int)source_nid, (int)path->source_index[i]);

			if (current->type == HDA_WIDGET_TYPE_AUDIO_MIXER) {
				printf(" (Mixer input)");
			} else if (current->num_connections > 1) {
				printf(" (Input selection)");
			}

			printf("\n");
		}
		*/
	}

	if (driver->output_paths_truncated[codec_addr]) {
		printf("[INTEL HDA]: WARNING: Output path discovery reached its configured limit.\n");
	}
}

static b8 intel_hda_driver_check_dac_format(intel_hda_driver_t* driver, u8 codec_addr, u8 dac_nid) {
	u32 pcm_caps = 0;
	u32 stream_caps = 0;

	printf("[INTEL HDA]: Checking PCM capabilities of DAC NID %d...\n", (int)dac_nid);

	if (!intel_hda_get_parameter(driver, codec_addr, dac_nid, HDA_PARAMETER_SUPPORTED_PCM_SIZE_RATES, &pcm_caps)) {
		printf("[INTEL HDA]: Failed to query PCM capabilities.\n");
		return false;
	}

	if (!intel_hda_get_parameter(driver, codec_addr, dac_nid, HDA_PARAMETER_SUPPORTED_STREAM_FORMATS, &stream_caps)) {
		printf("[INTEL HDA]: Failed to query stream format capabilities.\n");
		return false;
	}

	printf("[INTEL HDA]: PCM Capabilities: 0x%08x\n", (unsigned int)pcm_caps);
	printf("[INTEL HDA]: Stream Formats: 0x%08x\n", (unsigned int)stream_caps);

	b8 supports_16bit = (pcm_caps & HDA_PCM_SIZE_16BIT) != 0;
	b8 supports_48khz = (pcm_caps & HDA_PCM_RATE_48000) != 0;
	b8 supports_pcm = (stream_caps & HDA_STREAM_FORMAT_PCM) != 0;

	intel_hda_widget_t* dac = &driver->widgets[codec_addr][dac_nid];

	u8 channels = ((((dac->capabilities >> 12) & 0x0E) | (dac->capabilities & 0x01)) + 1);

	printf("[INTEL HDA]: 16-bit PCM: %s\n", supports_16bit ? "Supported" : "Not supported");
	printf("[INTEL HDA]: 48kHz: %s\n", supports_48khz ? "Supported" : "Not supported");
	printf("[INTEL HDA]: PCM: %s\n", supports_pcm ? "Supported" : "Not supported");
	printf("[INTEL HDA]: Maximum DAC channels: %d\n", (int)channels);

	if (!supports_16bit || !supports_48khz || !supports_pcm || channels < 2) {
		printf("[INTEL HDA]: DAC does not support our desired PCM format.\n");

		return false;
	}

	printf("[INTEL HDA]: 48kHz stereo 16-bit PCM is supported!\n");

	return true;
}

static b8 intel_hda_driver_check_output_stream(intel_hda_driver_t* driver) {
	if (!driver || !driver->regs) {
		return false;
	}

	if (driver->num_oss == 0) {
		printf("[INTEL HDA]: Controller has no output stream descriptors!\n");

		return false;
	}

	// the first output descriptor follows all input descriptors
	u32 stream_index = driver->num_iss;

	vaddr_t stream_base = (vaddr_t)driver->regs + HDA_REG_STREAM_DESC_BASE + (stream_index * HDA_STREAM_DESC_STRIDE);

	// SDnCTL is three bytes, read its individual bytes to avoid accidentally touching the adjacent write-one-to-clear status register
	u8 ctl0 = mmio_read8(stream_base + 0);
	u8 ctl1 = mmio_read8(stream_base + 1);
	u8 ctl2 = mmio_read8(stream_base + 2);

	u8 status = mmio_read8(stream_base + 3);

	u8 stream_tag = (ctl2 >> 4) & 0x0F;

	printf("[INTEL HDA]: OUTPUT STREAM DESCRIPTOR\n");
	printf("       Descriptor index: %d\n", (int)stream_index);
	printf("       Register offset: 0x%08x\n", (unsigned int)(HDA_REG_STREAM_DESC_BASE + (stream_index * HDA_STREAM_DESC_STRIDE)));
	printf("       Control: %02x %02x %02x\n", ctl0, ctl1, ctl2);
	printf("       Status: 0x%02x\n", status);
	printf("       RUN: %s\n", (ctl0 & HDA_SD_CTL_RUN) ? "Running" : "Stopped");
	printf("       Reset: %s\n", (ctl0 & 1) ? "Asserted" : "Deasserted");
	printf("       Stream tag: %d\n", (int)stream_tag);

	if (ctl0 & HDA_SD_CTL_RUN) {
		printf("[INTEL HDA]: Output stream descriptor is already running!\n");

		return false;
	}

	printf("[INTEL HDA]: Output stream descriptor is available for testing.\n");

	return true;
}

/*
 * HDA Specification Rev. 1.0a
 * Section 3.3.7, Table 8:
 *   CORB/RIRB RUN and all Stream RUN bits must be clear
 *   before asserting controller reset.
 *
 * Section 3.3.35, Table 34:
 *   Stream RUN is bit 1 of SDnCTL.
 */
static b8 intel_hda_driver_stop_dma_engines(intel_hda_driver_t* driver) {
	if (!driver || !driver->regs) {
		return false;
	}

	vaddr_t base = (vaddr_t)driver->regs;

	u32 total_streams = driver->num_iss + driver->num_oss + driver->num_bss;

	// disable controller-level interrupts
	mmio_write32(base + HDA_REG_INTCTL, 0);

	// stop command DMA engines
	mmio_write8(base + HDA_REG_CORBCTL, 0);
	mmio_write8(base + HDA_REG_RIRBCTL, 0);

	// stop all input, output and bidirectional stream engines
	for (u32 i = 0; i < total_streams; i++) {
		vaddr_t ctl_addr = base + HDA_REG_STREAM_DESC_BASE + (i * HDA_STREAM_DESC_STRIDE);

		// only write the low control byte, do not accidentally write back
		// adjacent W1C status bits.
		u8 ctl = mmio_read8(ctl_addr);

		mmio_write8(ctl_addr, ctl & ~HDA_SD_CTL_RUN);
	}

	// verify that all DMA engines have stopped
	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		b8 stopped = true;

		if (mmio_read8(base + HDA_REG_CORBCTL) & HDA_CORBCTL_RUN) {
			stopped = false;
		}

		if (mmio_read8(base + HDA_REG_RIRBCTL) & HDA_RIRBCTL_DMAEN) {
			stopped = false;
		}

		for (u32 i = 0; i < total_streams; i++) {
			vaddr_t ctl_addr = base + HDA_REG_STREAM_DESC_BASE + (i * HDA_STREAM_DESC_STRIDE);
			if (mmio_read8(ctl_addr) & HDA_SD_CTL_RUN) {
				stopped = false;
			}
		}

		if (stopped) {
			return true;
		}

		msleep(1);
	}

	printf("[INTEL HDA]: Timed out stopping DMA engines.\n");

	return false;
}

static b8 intel_hda_select_ring_size(vaddr_t base, u32 reg_offset, u8* selected_size, u16* entries) {
	if (!selected_size || !entries) {
		return false;
	}

	u8 reg = mmio_read8(base + reg_offset);

	// bits 7:4 are the supported-size bitmap
	u8 capabilities = (reg >> 4) & 0x07;

	// prefer 256 entries
	if (capabilities & 0x04) {
		*selected_size = 0x02;
		*entries = 256;
	}
	// otherwise use 16 entries
	else if (capabilities & 0x02) {
		*selected_size = 0x01;
		*entries = 16;
	}
	// fall back to 2 entries
	else if (capabilities & 0x01) {
		*selected_size = 0x00;
		*entries = 2;
	} else {
		printf("[INTEL HDA]: No supported ring sizes reported!\n");
		return false;
	}

	return true;
}

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.4
 *
 * Get Parameter (F00h)
 *
 * Executes a Get Parameter command against a codec node.
 */
static b8 intel_hda_get_parameter(intel_hda_driver_t* driver, u8 codec_addr, u8 node_id, u8 parameter, u32* response) {
	if (!response) {
		return false;
	}

	return intel_hda_driver_send_verb(driver, codec_addr, node_id, HDA_VERB_GET_PARAMETER, parameter, response);
}

static const char* intel_hda_widget_type_name(u8 type) {
	switch (type) {
		case HDA_WIDGET_TYPE_AUDIO_OUTPUT: {
			return "Audio Output";
		}
		case HDA_WIDGET_TYPE_AUDIO_INPUT: {
			return "Audio Input";
		}
		case HDA_WIDGET_TYPE_AUDIO_MIXER: {
			return "Audio Mixer";
		}
		case HDA_WIDGET_TYPE_AUDIO_SELECTOR: {
			return "Audio Selector";
		}
		case HDA_WIDGET_TYPE_PIN_COMPLEX: {
			return "Pin Complex";
		}
		case HDA_WIDGET_TYPE_POWER: {
			return "Power";
		}
		case HDA_WIDGET_TYPE_VOLUME_KNOB: {
			return "Volume Knob";
		}
		case HDA_WIDGET_TYPE_BEEP_GENERATOR: {
			return "Beep Generator";
		}
		case HDA_WIDGET_TYPE_VENDOR_DEFINED: {
			return "Vendor Defined";
		}
		default: {
			return "Unknown";
		}
	}
}

static const char* intel_hda_pin_device_name(u8 device) {
	switch (device) {
		case 0x0:
			return "Line Out";
		case 0x1:
			return "Speaker";
		case 0x2:
			return "Headphone";
		case 0x3:
			return "CD";
		case 0x4:
			return "SPDIF Out";
		case 0x5:
			return "Digital Other Out";
		case 0x6:
			return "Modem Line";
		case 0x7:
			return "Modem Handset";
		case 0x8:
			return "Line In";
		case 0x9:
			return "AUX";
		case 0xA:
			return "Microphone";
		case 0xB:
			return "Telephony";
		case 0xC:
			return "SPDIF In";
		case 0xD:
			return "Digital Other In";
		case 0xE:
			return "Reserved";
		case 0xF:
			return "Other";
		default:
			return "Unknown";
	}
}

static b8 intel_hda_driver_get_amp(intel_hda_driver_t* driver, u8 codec_addr, u8 nid, b8 output_amp, b8 left_channel, u8 index, u32* response) {
	if (!response || index > 0x0F) {
		return false;
	}

	/*
	 * Get Amplifier Gain/Mute payload:
	 *
	 * Bit 15: Output/Input (1 = output)
	 * Bit 14: Reserved
	 * Bit 13: Left/Right (1 = left)
	 * Bits 12:4: Reserved
	 * Bits 3:0: Connection index
	 */
	u16 payload = 0;

	if (output_amp) {
		payload |= (1u << 15);
	}

	if (left_channel) {
		payload |= (1u << 13);
	}

	payload |= index & 0x0F;

	return intel_hda_driver_send_verb4(driver, codec_addr, nid, HDA_VERB_GET_AMP_GAIN_MUTE, payload, response);
}

/*
 * HDA Specification Rev. 1.0a
 * Section 7.3.4.3, Figure 83
 *
 * Subordinate Node Count Response:
 *
 * Bits 23:16 = Starting Node Number
 * Bits 7:0   = Total Number of Nodes
 */
static b8 intel_hda_parse_node_count(u32 response, u8* start_node, u8* node_count) {
	if (!start_node || !node_count) {
		return false;
	}

	u8 start = (response >> 16) & 0xFF;
	u8 count = response & 0xFF;

	if (count == 0) {
		return false;
	}

	// Node IDs are eight bits wide, validate the end-exclusive range using u16 to avoid wrapping when the range ends at node 0xFF
	u16 end = (u16)start + (u16)count;

	if (end > 0x100) {
		return false;
	}

	*start_node = start;
	*node_count = count;

	return true;
}

/*
 * HDA Specification Rev. 1.0a
 *
 * Section 7.3.4.11:
 *   Connection List Length
 *
 * Section 7.3.3.3:
 *   Get Connection List Entry
 *
 * Section 7.1.2:
 *   Widget Interconnections and Connection Ranges
 *
 * Reads and expands a widget's complete connection list.
 *
 * Output:
 *   connections[] = expanded connected widget NIDs
 *   num_connections = number of expanded connections
 *
 * Returns false on malformed data, failed verbs, or insufficient output capacity.
 */
static b8 intel_hda_driver_get_connections(intel_hda_driver_t* driver, u8 codec_addr, u8 node_id, u8* connections, u16 max_connections, u16* num_connections) {
	if (!driver || !connections || !num_connections) {
		return false;
	}

	*num_connections = 0;

	// first retrieve the connection list length
	u32 length_response = 0;

	if (!intel_hda_get_parameter(driver, codec_addr, node_id, HDA_PARAMETER_CONNECTION_LIST_LENGTH, &length_response)) {
		return false;
	}

	b8 long_form = (length_response & HDA_CONNECTION_LIST_LONG_FORM) != 0;
	u16 raw_count = length_response & HDA_CONNECTION_LIST_LENGTH_MASK;

	if (raw_count == 0) {
		return true;
	}

	/*
	 * Select the appropriate connection encoding.
	 *
	 * Short:
	 *   Four 8-bit entries per response.
	 *
	 * Long:
	 *   Two 16-bit entries per response.
	 */
	u8 entry_bits = long_form ? 16 : 8;
	u8 entries_per_response = long_form ? 2 : 4;

	u32 entry_mask = long_form ? 0x7FFFu : 0x7Fu;

	u32 range_mask = 1u << (entry_bits - 1);

	u32 raw_entry_mask = long_form ? 0xFFFFu : 0xFFu;

	u16 previous_nid = 0;

	/*
	 * Fetch connection entries in batches, Get Connection List Entry requires:
	 * Long form: index must be a multiple of 2.
	 * Short form: index must be a multiple of 4.
	 */
	for (u16 index = 0; index < raw_count; index += entries_per_response) {
		u32 response = 0;

		if (!intel_hda_driver_send_verb(driver, codec_addr, node_id, HDA_VERB_GET_CONNECTION_LIST_ENTRY, (u8)index, &response)) {
			printf("[INTEL HDA]: Failed to retrieve connection list for NID %d.\n", (int)node_id);
			return false;
		}

		// decode each entry in this response
		for (u8 j = 0; j < entries_per_response && index + j < raw_count; j++) {
			u8 shift = j * entry_bits;
			u32 raw_entry = (response >> shift) & raw_entry_mask;
			b8 is_range = (raw_entry & range_mask) != 0;
			u16 nid = raw_entry & entry_mask;

			// HDA codec command node IDs are eight bits; reject invalid connection targets instead of silently truncating them
			if (nid > HDA_NODE_ID_MAX) {
				printf("[INTEL HDA]: Invalid connection NID 0x%x on widget %d.\n", (unsigned int)nid, (int)node_id);
				return false;
			}

			if (is_range) {
				 // a range entry extends from previous_nid + 1 through nid, inclusive
				if (previous_nid == 0 || previous_nid >= nid) {
					printf("[INTEL HDA]: Invalid connection range %d-%d on widget " "%d.\n", (int)previous_nid, (int)nid, (int)node_id);
					return false;
				}

				for (u16 n = previous_nid + 1; n <= nid; n++) {
					if (*num_connections >= max_connections) {
						printf("[INTEL HDA]: Connection buffer exhausted.\n");
						return false;
					}

					connections[*num_connections] = (u8)n;
					(*num_connections)++;
				}
			} else {
				if (*num_connections >= max_connections) {
					printf("[INTEL HDA]: Connection buffer exhausted.\n");
					return false;
				}

				connections[*num_connections] = (u8)nid;
				(*num_connections)++;
			}

			previous_nid = nid;
		}
	}

	return true;
}

/*
 * HDA Specification Rev. 1.0a
 *
 * Section 7.3.4.9: Pin Capabilities
 * Section 7.3.3.31: Configuration Default
 *
 * Enumerates only useful analog output candidates.
 */
static b8 intel_hda_driver_enumerate_pin(intel_hda_driver_t* driver, u8 codec_addr, u8 node_id, u32 widget_caps) {
	u32 pin_caps = 0;

	if (!intel_hda_get_parameter(driver, codec_addr, node_id, HDA_PARAMETER_PIN_CAPABILITIES, &pin_caps)) {
		return false;
	}

	intel_hda_widget_t* widget = &driver->widgets[codec_addr][node_id];

	widget->pin_capabilities = pin_caps;
	widget->has_pin_caps = true;

	u32 config = 0;

	if (!intel_hda_driver_send_verb(driver, codec_addr, node_id, HDA_VERB_GET_CONFIG_DEFAULT, 0, &config)) {
		return false;
	}

	widget->pin_config = config;
	widget->has_pin_config = true;

	// Decode Configuration Default.
	u8 connectivity = (config >> 30) & 0x03;
	u8 location = (config >> 24) & 0x3F;
	u8 device = (config >> 20) & 0x0F;
	u8 connection = (config >> 16) & 0x0F;
	u8 color = (config >> 12) & 0x0F;
	u8 association = (config >> 4) & 0x0F;
	u8 sequence = config & 0x0F;

	/*
	 * only consider line-out, speaker and headphone pins
	 *
	 * HDA Specification Rev. 1.0a
	 * Section 7.3.3.31, Table 111.
	 */
	b8 output_device = device == 0x0 || device == 0x1 || device == 0x2;

	/*
	 * connectivity:
	 * 0 = Physical jack
	 * 1 = No physical connection
	 * 2 = Fixed device
	 * 3 = Jack and fixed device
	 */
	if (!output_device || !(pin_caps & HDA_PINCAP_OUTPUT) ||
		connectivity == 1) {
		return true;
	}

	static const char* locations[] = { "External", "Internal", "Separate chassis", "Other" };

	static const char* positions[] = {
		"N/A", "Rear", "Front", "Left", "Right", "Top",
		"Bottom", "Special", "Special", "Special", "Reserved", "Reserved",
		"Reserved", "Reserved", "Reserved", "Reserved"
	};

	static const char* colors[] = {
		"Unknown", "Black", "Grey", "Blue", "Green", "Red",
		"Orange", "Yellow", "Purple", "Pink", "Reserved", "Reserved",
		"Reserved", "Reserved", "White", "Unknown"
	};

	static const char* connection_types[] = {
		"Unknown", "3.5mm", "6.35mm",
		"ATAPI", "RCA", "Optical",
		"Other Digital", "Other Analog", "Multichannel DIN",
		"XLR", "RJ11", "Combination",
		"Reserved", "Reserved", "Reserved",
		"Other"
	};

	/*
	printf(
		"\n[INTEL HDA]: OUTPUT CANDIDATE - NID %d (%s)\n",
		(int)node_id,
		intel_hda_pin_device_name(device)
	);

	printf(
		"       Configuration: 0x%08x\n",
		(unsigned int)config
	);

	printf(
		"       Location: %s %s\n",
		locations[location >> 4],
		positions[location & 0x0F]
	);

	printf(
		"       Connection: %s | Color: %s\n",
		connection_types[connection],
		colors[color]
	);

	printf(
		"       Association: %d | Sequence: %d\n",
		(int)association,
		(int)sequence
	);

	printf(
		"       Pin Capabilities: 0x%08x\n",
		(unsigned int)pin_caps
	);

	printf(
		"       Headphone Drive: %s | Presence Detect: %s\n",
		(pin_caps & HDA_PINCAP_HEADPHONE_DRIVE) ? "Yes" : "No",
		(pin_caps & HDA_PINCAP_PRESENCE_DETECT) ? "Yes" : "No"
	);
	*/

	// retrieve and display connections only for output candidates
	if (widget_caps & HDA_WIDGET_CAP_CONN_LIST) {
		u8 connections[HDA_MAX_CONNECTIONS];
		u16 num_connections = 0;

		if (!intel_hda_driver_get_connections(driver, codec_addr, node_id, connections, HDA_MAX_CONNECTIONS, &num_connections)) {
			printf("       Failed to retrieve connections.\n");
			return false;
		}

		printf("       Connected widgets:");

		for (u16 i = 0; i < num_connections; i++) {
			u32 connected_caps = 0;
			if (intel_hda_get_parameter(driver, codec_addr, connections[i], HDA_PARAMETER_AUDIO_WIDGET_CAPS, &connected_caps)) {
				u8 connected_type = (connected_caps & HDA_WIDGET_CAP_TYPE_MASK) >> HDA_WIDGET_CAP_TYPE_SHIFT;
				printf(" NID %d (%s)", (int)connections[i], intel_hda_widget_type_name(connected_type));
			} else {
				printf(" NID %d (Unknown)", (int)connections[i]);
			}

			if (i + 1 < num_connections) {
				printf(",");
			}
		}

		printf("\n");
	} else {
		printf("       No connection list capability.\n");
	}

	return true;
}

static b8 intel_hda_path_contains(const u8* path, u8 depth, u8 nid) {
	for (u8 i = 0; i < depth; i++) {
		if (path[i] == nid) {
			return true;
		}
	}

	return false;
}

static void intel_hda_driver_store_output_path(intel_hda_driver_t* driver, u8 codec_addr, const u8* reverse_nodes, const u8* reverse_edge_indices, u8 reverse_depth) {
	if (!driver || !driver->output_paths[codec_addr]) {
		return;
	}

	if (driver->output_path_count[codec_addr] >=
		HDA_MAX_OUTPUT_PATHS_PER_CODEC) {
		driver->output_paths_truncated[codec_addr] = true;
		return;
	}

	// the DFS traverses: Pin -> ... -> DAC | convert that into: DAC -> ... -> Pin
	u8 path_length = reverse_depth + 1;

	if (path_length > HDA_MAX_PATH_DEPTH) {
		return;
	}

	intel_hda_output_path_t* output = &driver->output_paths[codec_addr][driver->output_path_count[codec_addr]];

	memset(output, 0, sizeof(*output));

	output->valid = true;
	output->depth = path_length;

	output->dac_nid = reverse_nodes[reverse_depth];
	output->pin_nid = reverse_nodes[0];

	// copy the route in playback direction
	for (u8 i = 0; i < path_length; i++) {
		u8 reverse_index = reverse_depth - i;
		output->nodes[i] = reverse_nodes[reverse_index];

		// for every node except the DAC, record the connection-list index that selects its source
		if (i > 0) {
			output->source_index[i] = reverse_edge_indices[reverse_index];
		}
	}

	// save pin configuration metadata
	intel_hda_widget_t* pin = &driver->widgets[codec_addr][output->pin_nid];

	if (pin->valid && pin->has_pin_config) {
		output->pin_config = pin->pin_config;
		output->pin_device = (pin->pin_config >> 20) & 0x0F;
		output->association = (pin->pin_config >> 4) & 0x0F;
		output->sequence = pin->pin_config & 0x0F;
		output->function_group_nid = pin->function_group_nid;
	}

	driver->output_path_count[codec_addr]++;
}

static void intel_hda_driver_print_amp_caps(const char* name, u32 caps) {
	u8 offset = caps & 0x7F;
	u8 num_steps = (caps >> 8) & 0x7F;
	u8 step_size = (caps >> 16) & 0x7F;
	b8 mute = (caps & (1u << 31)) != 0;

	printf("       %s: 0x%08x\n", name, (unsigned int)caps);
	printf("           Mute capable: %s\n", mute ? "Yes" : "No");
	printf("           Gain levels: %d\n", (int)num_steps + 1);
	printf("           Step size: %d quarter-dB units\n", (int)step_size + 1);
	printf("           0 dB gain code: %d\n", (int)offset);
}

static void intel_hda_driver_dump_route_widget(intel_hda_driver_t* driver, u8 codec_addr, u8 nid) {
	if (!driver || !driver->widgets[codec_addr]) {
		return;
	}

	intel_hda_widget_t* widget = &driver->widgets[codec_addr][nid];

	if (!widget->valid) {
		return;
	}

	printf("[INTEL HDA]: ROUTE NODE NID %d (%s)\n", (int)nid, intel_hda_widget_type_name(widget->type));
	printf("       Widget capabilities: 0x%08x\n", (unsigned int)widget->capabilities);

	// print amplifier capabilities
	if (widget->has_input_amp_caps) {
		intel_hda_driver_print_amp_caps("Input amplifier", widget->input_amp_caps);
	}

	if (widget->has_output_amp_caps) {
		intel_hda_driver_print_amp_caps("Output amplifier", widget->output_amp_caps);
	}

	// inspect power capabilities and current power state
	if (widget->capabilities & HDA_WIDGET_CAP_POWER_CTRL) {
		u32 supported_states = 0;
		if (intel_hda_get_parameter(driver, codec_addr, nid, HDA_PARAMETER_SUPPORTED_POWER_STATES, &supported_states)) {
			printf("       Supported power states: 0x%08x\n", (unsigned int)supported_states);
		}

		u32 power_state = 0;
		if (intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_GET_POWER_STATE, 0, &power_state)) {
			printf("       Power state: Setting D%d | Actual D%d\n", (int)(power_state & 0x0F), (int)((power_state >> 4) & 0x0F));
		}
	}

	// inspect Pin Widget Control
	if (widget->type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
		u32 pin_control = 0;

		if (intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_GET_PIN_WIDGET_CONTROL, 0, &pin_control)) {
			printf("       Pin Widget Control: 0x%02x\n", (unsigned int)(pin_control & 0xFF));
			printf("       Output enable: %s\n", (pin_control & (1u << 6)) ? "Yes" : "No");
			printf("       Headphone enable: %s\n", (pin_control & (1u << 7)) ? "Yes" : "No");
		}

		// inspect EAPD only if supported
		if (widget->has_pin_caps &&
			(widget->pin_capabilities & HDA_PINCAP_EAPD)) {
			u32 eapd = 0;

			if (intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_GET_EAPD_BTL_ENABLE, 0, &eapd)) {
				printf("       EAPD/BTL: 0x%02x | EAPD: %s | BTL: %s\n", (unsigned int)(eapd & 0xFF), (eapd & (1u << 1)) ? "Enabled" : "Disabled", (eapd & (1u << 0)) ? "Enabled" : "Disabled");
			}
		}
	}
}

static void intel_hda_driver_dump_amp_state(intel_hda_driver_t* driver, u8 codec_addr, u8 nid, b8 output_amp, u8 index) {
	u32 response = 0;

	printf("[INTEL HDA]: AMP STATE | NID %d | %s | Index %d\n", (int)nid, output_amp ? "Output" : "Input", (int)index);

	for (u8 channel = 0; channel < 2; channel++) {
		b8 left = (channel == 0);

		if (!intel_hda_driver_get_amp(driver, codec_addr, nid, output_amp, left, index, &response)) {
			printf("       Failed to query channel.\n");
			continue;
		}

		u8 gain = response & 0x7F;
		b8 muted = (response & (1u << 7)) != 0;

		printf("       %s | Gain: %d | Muted: %s | Response: 0x%08x\n", left ? "Left" : "Right", (int)gain, muted ? "Yes" : "No", (unsigned int)response);
	}
}

static void intel_hda_driver_dump_playback_pin(intel_hda_driver_t* driver, u8 codec_addr, u8 nid) {
	intel_hda_widget_t* widget = &driver->widgets[codec_addr][nid];

	if (!widget->valid || widget->type != HDA_WIDGET_TYPE_PIN_COMPLEX) {
		return;
	}

	printf("[INTEL HDA]: PLAYBACK PIN DIAGNOSTIC NID %d\n", (int)nid);

	// output amplifier
	printf("       Output amplifier available: %s\n", widget->has_output_amp_caps ? "Yes" : "No");

	if (widget->has_output_amp_caps) {
		intel_hda_driver_print_amp_caps("Output amplifier", widget->output_amp_caps);

		intel_hda_driver_dump_amp_state(driver, codec_addr, nid, true, 0);
	}

	// Pin Widget Control
	u32 pin_control = 0;

	if (intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_GET_PIN_WIDGET_CONTROL, 0, &pin_control)) {
		printf("       Pin Control: 0x%02x\n", (unsigned int)(pin_control & 0xFF));
		printf("       Output Enable: %s\n", (pin_control & (1u << 6)) ? "Yes" : "No");
		printf("       Headphone Enable: %s\n", (pin_control & (1u << 7)) ? "Yes" : "No");
	}

	// EAPD
	if (widget->has_pin_caps && (widget->pin_capabilities & HDA_PINCAP_EAPD)) {
		u32 eapd = 0;

		if (intel_hda_driver_send_verb(driver, codec_addr, nid, HDA_VERB_GET_EAPD_BTL_ENABLE, 0, &eapd)) {
			printf("       EAPD: %s\n", (eapd & (1u << 1)) ? "Enabled" : "Disabled");
		}
	}
}

/*
 * Follow widget connections toward an Audio Output converter
 * Traversal direction:
 *     Pin -> Mixer -> Selector -> DAC
 * Printed playback direction:
 *     DAC -> Selector -> Mixer -> Pin
 */
static void intel_hda_driver_find_output_paths(
	intel_hda_driver_t* driver, u8 codec_addr, u8 root_pin, u8 current_nid,
	u8* reverse_nodes, u8* reverse_edge_indices, u8 depth) {
	if (!driver || !reverse_nodes || !reverse_edge_indices) {
		return;
	}

	if (depth >= HDA_MAX_PATH_DEPTH) {
		driver->output_paths_truncated[codec_addr] = true;
		return;
	}

	// prevent cycles in the current traversal
	if (intel_hda_path_contains(reverse_nodes, depth, current_nid)) {
		return;
	}

	intel_hda_widget_t* widget = &driver->widgets[codec_addr][current_nid];

	if (!widget->valid) {
		return;
	}

	// save the current node
	reverse_nodes[depth] = current_nid;

	// reached an Audio Output Converter, this is a valid playback path endpoint
	if (widget->type == HDA_WIDGET_TYPE_AUDIO_OUTPUT) {
		intel_hda_driver_store_output_path(driver, codec_addr, reverse_nodes, reverse_edge_indices, depth);
		return;
	}

	// audio input converters are capture endpoints
	if (widget->type == HDA_WIDGET_TYPE_AUDIO_INPUT) {
		return;
	}

	// do not traverse through another Pin Complex, the root pin is our intended physical endpoint.
	if (widget->type == HDA_WIDGET_TYPE_PIN_COMPLEX &&
		current_nid != root_pin) {
		return;
	}

	// only traverse actual signal-routing widgets, power widgets have connection lists describing power associations, not audio signal connections
	switch (widget->type) {
		case HDA_WIDGET_TYPE_PIN_COMPLEX:
		case HDA_WIDGET_TYPE_AUDIO_MIXER:
		case HDA_WIDGET_TYPE_AUDIO_SELECTOR:
			break;

		default:
			return;
	}

	if (!widget->connections || widget->num_connections == 0) {
		return;
	}

	// follow each possible upstream connection
	for (u16 i = 0; i < widget->num_connections; i++) {
		u8 source_nid = widget->connections[i];

		// a connection index ranges from 0 through 255
		if (i > 0xFF) {
			driver->output_paths_truncated[codec_addr] = true;
			return;
		}

		// record the index in the CURRENT widget's connection list; this becomes source_index[] when the completed path is reversed.
		reverse_edge_indices[depth] = (u8)i;

		intel_hda_driver_find_output_paths(driver, codec_addr, root_pin, source_nid, reverse_nodes, reverse_edge_indices, depth + 1);

		if (driver->output_path_count[codec_addr] >=
			HDA_MAX_OUTPUT_PATHS_PER_CODEC) {
			driver->output_paths_truncated[codec_addr] = true;
			return;
		}
	}
}

static b8 intel_hda_driver_start_default_analog_output(
	intel_hda_driver_t* driver) {
	if (!driver) {
		return false;
	}

	// one playback stream per controller for now
	if (driver->playback_prepared || driver->playback_running) {
		printf("[INTEL HDA]: Playback is already initialized.\n");
		return false;
	}

	// search all codecs attached to this controller
	for (u8 codec_addr = 0; codec_addr < HDA_MAX_CODECS; codec_addr++) {
		if (!(driver->codec_mask & (1u << codec_addr))) {
			continue;
		}

		if (!driver->widgets[codec_addr] || !driver->output_paths[codec_addr]) {
			continue;
		}

		/*
		 * Find the preferred analog endpoint
		 * Priority:
		 * 1. Line Out
		 * 2. Headphone
		 * 3. Speaker
		 */
		intel_hda_output_path_t* path = intel_hda_driver_find_default_analog_output(driver, codec_addr);

		if (!path) {
			continue;
		}

		printf("[INTEL HDA]: Selected default analog output:\n");

		printf("       Codec: %d\n", (int)codec_addr);
		printf("       DAC: NID %d\n", (int)path->dac_nid);
		printf("       Pin: NID %d\n", (int)path->pin_nid);
		printf("       Path depth: %d\n", (int)path->depth);

		// verify that the DAC supports our initial format
		if (!intel_hda_driver_check_dac_format(driver, codec_addr, path->dac_nid)) {
			printf("[INTEL HDA]: Selected DAC does not support our test format.\n");
			continue;
		}

		// configure the discovered route
		if (!intel_hda_driver_configure_output_path(driver, codec_addr, path, true)) {
			printf("[INTEL HDA]: Failed to configure selected output path.\n");
			return false;
		}

		// prepare the controller playback stream, the DAC NID comes from the discovered route
		if (!intel_hda_driver_prepare_output_stream(driver, codec_addr, path->dac_nid)) {
			printf("[INTEL HDA]: Failed to prepare playback stream.\n");
			return false;
		}

		// start playback
		if (!intel_hda_driver_start_output_stream(driver)) {
			printf("[INTEL HDA]: Failed to start playback stream.\n");
			return false;
		}

		printf("[INTEL HDA]: Default analog output initialized successfully!\n");

		return true;
	}

	printf("[INTEL HDA]: No usable analog output found on this controller.\n");

	return false;
}

// reference: linux
b8 intel_hda_driver_reset_controller(intel_hda_driver_t* driver) {
	if (!driver || !driver->regs) {
		return false;
	}

	vaddr_t base = (vaddr_t)driver->regs;

	// clear pending codec status, write-one-to-clear semantics
	mmio_write16(base + HDA_REG_STATESTS, HDA_STATESTS_CODEC_MASK);

	// enter reset
	u32 gctl = mmio_read32(base + HDA_REG_GCTL);

	if (gctl & HDA_GCTL_CRST) {
		if (!intel_hda_driver_stop_dma_engines(driver)) {
			return false;
		}
	}

	mmio_write32(base + HDA_REG_GCTL, gctl & ~HDA_GCTL_CRST);

	u32 timeout = HDA_TIMEOUT_MS;

	while (mmio_read32(base + HDA_REG_GCTL) & HDA_GCTL_CRST) {
		if (timeout-- == 0) {
			printf("[INTEL HDA]: Timeout entering link reset.\n");
			return false;
		}

		msleep(1);
	}

	// allow the link to settle
	msleep(1);

	// exit reset
	gctl = mmio_read32(base + HDA_REG_GCTL);

	mmio_write32(base + HDA_REG_GCTL, gctl | HDA_GCTL_CRST);

	timeout = HDA_TIMEOUT_MS;

	while (!(mmio_read32(base + HDA_REG_GCTL) & HDA_GCTL_CRST)) {
		if (timeout-- == 0) {
			printf("[INTEL HDA]: Timeout exiting link reset.\n");
			return false;
		}

		msleep(1);
	}

	// wait for codec link initialization
	msleep(2);

	return true;
}

b8 intel_hda_driver_init_command_dma(intel_hda_driver_t* driver) {
	if (!driver || !driver->regs) {
		return false;
	}

	vaddr_t base = (vaddr_t)driver->regs;

	// stop both DMA engines before changing their configuration
	mmio_write8(base + HDA_REG_CORBCTL, 0);
	mmio_write8(base + HDA_REG_RIRBCTL, 0);

	u32 timeout = HDA_TIMEOUT_MS;

	while (mmio_read8(base + HDA_REG_CORBCTL) & HDA_CORBCTL_RUN || mmio_read8(base + HDA_REG_RIRBCTL) & HDA_RIRBCTL_DMAEN) {
		if (timeout-- == 0) {
			printf("[INTEL HDA]: Timed out stopping command DMA.\n");
			return false;
		}

		msleep(1);
	}

	// determine supported ring sizes
	u8 corb_size = 0, rirb_size = 0;
	if (!intel_hda_select_ring_size(base, HDA_REG_CORBSIZE, &corb_size, &driver->corb_entries)) {
		return false;
	}

	if (!intel_hda_select_ring_size(base, HDA_REG_RIRBSIZE, &rirb_size, &driver->rirb_entries)) {
		return false;
	}

	// TODO: dma_free, boundaries, alignment
	dma_region_t region = dma_alloc(HDA_COMMAND_DMA_SIZE);

	if (!region.virt || region.size < HDA_COMMAND_DMA_SIZE) {
		printf("[INTEL HDA]: Failed to allocate command DMA memory.\n");
		return false;
	}

	if ((region.phys & (HDA_COMMAND_DMA_ALIGNMENT - 1)) != 0) {
		printf(
			"[INTEL HDA]: DMA physical address is insufficiently aligned.\n");
		return false;
	}

	if ((region.virt & (HDA_COMMAND_DMA_ALIGNMENT - 1)) != 0) {
		printf("[INTEL HDA]: DMA virtual address is insufficiently aligned.\n");
		return false;
	}

	if (region.phys > (~(u64)0 - (HDA_COMMAND_DMA_SIZE - 1))) {
		printf("[INTEL HDA]: DMA physical address range overflow.\n");
		return false;
	}

	if (!driver->supports_64bit_dma &&
		region.phys > 0xFFFFFFFFULL - (HDA_COMMAND_DMA_SIZE - 1)) {
		printf("[KERNEL HDA]: DMA allocation exceeds 32-bit address range.\n");
		return false;
	}

	// clear the complete allocation before handing to hardware
	memset((void*)region.virt, 0, HDA_COMMAND_DMA_SIZE);

	// store allocation info
	driver->command_dma_virt = region.virt;
	driver->command_dma_phys = region.phys;
	driver->command_dma_size = region.size;

	// CORB starts at offset 0, RIRB starts at offset 2048
	driver->corb = (intel_hda_corb_entry_t*)region.virt;

	driver->rirb = (intel_hda_rirb_entry_t*)(region.virt + HDA_RIRB_OFFSET);

	driver->corb_phys = region.phys;
	driver->rirb_phys = region.phys + HDA_RIRB_OFFSET;

	driver->corb_wp = 0;
	driver->rirb_rp = 0;

	// program ring sizes; the upper capability bits are read-only, only the low two are writable
	mmio_write8(base + HDA_REG_CORBSIZE, corb_size);
	mmio_write8(base + HDA_REG_RIRBSIZE, rirb_size);

	if ((mmio_read8(base + HDA_REG_CORBSIZE) & 0x03) != corb_size) {
		printf("[INTEL HDA]: Failed to configure CORB size.\n");
		return false;
	}

	if ((mmio_read8(base + HDA_REG_RIRBSIZE) & 0x03) != rirb_size) {
		printf("[INTEL HDA]: Failed to configure RIRB size.\n");
		return false;
	}

	// program CORB physical address
	if (driver->supports_64bit_dma) {
		mmio_write32(base + HDA_REG_CORBUBASE, (u32)(driver->corb_phys >> 32));
	}

	mmio_write32(base + HDA_REG_CORBLBASE, (u32)driver->corb_phys);

	// program RIRB physical address
	if (driver->supports_64bit_dma) {
		mmio_write32(base + HDA_REG_RIRBUBASE, (u32)(driver->rirb_phys >> 32));
	}

	mmio_write32(base + HDA_REG_RIRBLBASE, (u32)driver->rirb_phys);

	/*
	 * HDA Specification Rev. 1.0a
	 * Section 3.3.21, Table 21
	 *
	 * Standard CORBRP reset:
	 * 1. Write CORBRPRST = 1.
	 * 2. Wait for hardware acknowledgment.
	 * 3. Write CORBRPRST = 0.
	 * 4. Wait for reset deassertion.
	 *
	 * NVIDIA QUIRK:
	 *
	 * NVIDIA HDA controllers have a self-clearing CORBRP reset bit.
	 * They do not reliably follow the standard observable
	 * set/clear handshake.
	 *
	 * Linux reference:
	 *   sound/hda/hda_controller.c
	 *   AZX_DCAPS_CORBRP_SELF_CLEAR
	 *
	 * For NVIDIA:
	 *   - Still issue the reset request.
	 *   - Do NOT wait for the bit to become 1.
	 *   - Do NOT explicitly write zero afterward.
	 */
	mmio_write16(base + HDA_REG_CORBRP, HDA_CORBRP_RST);

	if (!driver->corbrp_self_clear) {
		u32 timeout = HDA_TIMEOUT_MS;
		b8 acknowledged = false;
		while (timeout--) {
			if (mmio_read16(base + HDA_REG_CORBRP) & HDA_CORBRP_RST) {
				acknowledged = true;
				break;
			}

			msleep(1);
		}

		if (!acknowledged) {
			printf("[INTEL HDA]: CORBRP reset acknowledgment timed out.\n");
			return false;
		}

		// hardware acknowledged the reset, deassert it
		mmio_write16(base + HDA_REG_CORBRP, 0);

		timeout = HDA_TIMEOUT_MS;

		b8 deasserted = false;

		while (timeout--) {
			if (!(mmio_read16(base + HDA_REG_CORBRP) & HDA_CORBRP_RST)) {
				deasserted = true;
				break;
			}

			msleep(1);
		}

		if (!deasserted) {
			printf("[INTEL HDA]: CORBRP reset deassertion timed out.\n");
			return false;
		}
	} else {
		printf("[INTEL HDA]: Using NVIDIA CORBRP self-clear quirk.\n");
	}

	/*
	 * HDA Specification Rev. 1.0a
	 * Section 3.3.20, Table 20
	 *
	 * Reset the software-visible CORB write pointer.
	 */
	mmio_write16(base + HDA_REG_CORBWP, 0);

	driver->corb_wp = 0;

	// reset RIRB hardware write pointer; RIRBWP reset is write-only, hardware clear the pointer to zero
	mmio_write16(base + HDA_REG_RIRBWP, HDA_RIRBWP_RST);

	driver->rirb_rp = 0;

	// configure response interrupt count; polling, so RIRB interrupts remain disabled
	mmio_write16(base + HDA_REG_RINTCNT, 1);

	// clear any pending status flags (write-one-to-clear semantics)
	mmio_write8(base + HDA_REG_CORBSTS, HDA_CORBSTS_CMEI);

	mmio_write8(base + HDA_REG_RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_OIS);

	// enable RIRB first, so responses can be received as soon as CORB starts transmitting commands
	mmio_write8(base + HDA_REG_RIRBCTL, HDA_RIRBCTL_DMAEN);

	if (!(mmio_read8(base + HDA_REG_RIRBCTL) & HDA_RIRBCTL_DMAEN)) {
		printf("[INTEL HDA]: Failed to enable RIRB DMA.\n");
		return false;
	}

	// enable CORB
	mmio_write8(base + HDA_REG_CORBCTL, HDA_CORBCTL_RUN);

	if (!(mmio_read8(base + HDA_REG_CORBCTL) & HDA_CORBCTL_RUN)) {
		printf("[INTEL HDA]: Failed to enable CORB DMA.\n");
		return false;
	}

	driver->command_dma_ready = true;

	printf("[INTEL HDA]: CORB initialized (%d entries), RIRB initialized (%d " "entries).\n", (int)driver->corb_entries, (int)driver->rirb_entries);

	return true;
}

static b8 intel_hda_driver_send_command(intel_hda_driver_t* driver, u8 codec_addr, u32 command, u32* response) {
	if (!driver || !driver->command_dma_ready || !driver->regs || !response) {
		return false;
	}

	if (codec_addr >= HDA_MAX_CODECS || !(driver->codec_mask & (1u << codec_addr))) {
		return false;
	}

	vaddr_t base = (vaddr_t)driver->regs;

	// find a free CORB slot, the next slot after CORBWP is available for writing, provided it does not collide with the hardware read pointer
	u16 next_wp = 0;
	b8 slot_available = false;

	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		if (mmio_read8(base + HDA_REG_CORBSTS) & HDA_CORBSTS_CMEI) {
			mmio_write8(base + HDA_REG_CORBSTS, HDA_CORBSTS_CMEI);

			printf("[INTEL HDA]: CORB memory error.\n");
			return false;
		}

		u16 hw_rp = mmio_read16(base + HDA_REG_CORBRP) & 0xFF;

		if (hw_rp >= driver->corb_entries) {
			return false;
		}

		next_wp = (driver->corb_wp + 1) % driver->corb_entries;

		if (next_wp != hw_rp) {
			slot_available = true;
			break;
		}

		msleep(1);
	}

	if (!slot_available) {
		printf("[INTEL HDA]: Timed out waiting for a CORB slot.\n");
		return false;
	}

	// write the command into the DMA buffer
	driver->corb[next_wp].command = command;

	// make the DMA-buffer write visible before updating CORBWP
	barrier_io_write();

	// advance the hardware write pointer.
	driver->corb_wp = next_wp;

	mmio_write16(base + HDA_REG_CORBWP, driver->corb_wp);

	// wait for a solicited response from the target codec, the RIRB may also contain unsolicited responses, so distinguish those using the extended response flags
	for (u32 timeout = 0; timeout < HDA_TIMEOUT_MS; timeout++) {
		// check for RIRB overflow
		if (mmio_read8(base + HDA_REG_RIRBSTS) & HDA_RIRBSTS_OIS) {
			mmio_write8(base + HDA_REG_RIRBSTS, HDA_RIRBSTS_OIS);

			printf("[INTEL HDA]: RIRB response overflow!\n");
			return false;
		}

		// read hardware write pointer
		u16 hw_wp = mmio_read16(base + HDA_REG_RIRBWP) & 0xFF;

		if (hw_wp >= driver->rirb_entries) {
			printf("[INTEL HDA]: Invalid RIRB write pointer.\n");
			return false;
		}

		// ensure DMA writes are visible before consuming RIRB data
		barrier_io_read();

		// process every entry that hardware has written
		while (driver->rirb_rp != hw_wp) {
			u16 next_rp = (driver->rirb_rp + 1) % driver->rirb_entries;
			u32 response_data = driver->rirb[next_rp].response;
			u32 response_extended = driver->rirb[next_rp].extended;

			driver->rirb_rp = next_rp;

			// ignore unsolicited responses for now
			if (response_extended & HDA_RIRB_UNSOL) {
				continue;
			}

			// check codec address;  the low four bits identify the codec that responded
			u8 response_codec = response_extended & HDA_RIRB_CODEC_MASK;

			if (response_codec != codec_addr) {
				continue;
			}

			// found solicited response
			*response = response_data;
			return true;
		}

		msleep(1);
	}

	printf("[INTEL HDA]: Verb timed out, (codec: %d, command: 0x%08x)\n",
		   (int)codec_addr, command);

	return false;
}

b8 intel_hda_driver_send_verb(intel_hda_driver_t* driver, u8 codec_addr, u8 node_id, u16 verb, u8 payload, u32* response) {
	if (verb > 0x0FFF) {
		return false;
	}

	u32 command = ((u32)codec_addr << 28) | ((u32)node_id << 20) | ((u32)verb << 8) | payload;

	return intel_hda_driver_send_command(driver, codec_addr, command, response);
}

b8 intel_hda_driver_send_verb4(intel_hda_driver_t* driver, u8 codec_addr, u8 node_id, u8 verb, u16 payload, u32* response) {
	if (verb > 0x0F) {
		return false;
	}

	u32 command = ((u32)codec_addr << 28) | ((u32)node_id << 20) | ((u32)verb << 16) | payload;

	return intel_hda_driver_send_command(driver, codec_addr, command, response);
}

b8 intel_hda_driver_init_driver(void* self) {
	intel_hda_driver_t* driver = (intel_hda_driver_t*)self;

	if (!driver || !driver->pci_device) {
		return false;
	}

	PCI_Device* dev = driver->pci_device;

	// enable PCI memory-space decoding
	u16 pci_command = pci_config_read16(dev->bus, dev->device, dev->function, PCI_CONFIG_COMMAND);

	pci_config_write16(dev->bus, dev->device, dev->function, PCI_CONFIG_COMMAND, pci_command | PCI_COMMAND_MEMORY);

	// enable PCI bus mastering for DMA.
	pci_enable_bus_master(dev);

	// verify BAR0.
	if (!dev->bar[0].valid || dev->bar[0].is_io) {
		printf("[INTEL HDA]: BAR0 is not a valid MMIO BAR.\n");
		return false;
	}

	if (dev->bar[0].size < 0x80) {
		printf("[INTEL HDA]: BAR0 is too small.\n");
		return false;
	}

	// map HDA controller registers.
	vaddr_t hda_base = pci_map_bar(dev, 0);

	if (!hda_base) {
		printf("[INTEL HDA]: Failed to map BAR0.\n");
		return false;
	}

	/*
	 * NVIDIA HDA controller quirk.
	 *
	 * Linux reference:
	 * AZX_DCAPS_CORBRP_SELF_CLEAR
	 *
	 * NVIDIA controllers may self-clear CORBRP.RST.
	 */
	driver->corbrp_self_clear = (driver->pci_device->vendor_id == HDA_PCI_VENDOR_NVIDIA);

	driver->regs = (volatile intel_hda_regs_t*)hda_base;

	// read controller capabilities
	u16 gcap = mmio_read16(hda_base + HDA_REG_GCAP);

	/*
	 * HDA Specification Rev. 1.0a
	 * Section 3.3.2, Table 3: GCAP
	 */
	driver->num_oss = (gcap >> 12) & 0x0F;
	driver->num_iss = (gcap >> 8) & 0x0F;
	driver->num_bss = (gcap >> 3) & 0x1F;

	driver->supports_64bit_dma = (gcap & (1u << 0)) != 0;

	// verify BAR0 covers all advertised stream descriptors
	u32 total_streams = driver->num_iss + driver->num_oss + driver->num_bss;

	u64 required_bar_size = 0x80 + ((u64)total_streams * 0x20);

	if (dev->bar[0].size < required_bar_size) {
		printf("[INTEL HDA]: BAR0 does not cover all stream descriptors.\n");
		return false;
	}

	// version information
	u8 vmaj = mmio_read8(hda_base + 0x03);
	u8 vmin = mmio_read8(hda_base + 0x02);

	printf("[INTEL HDA]: Version %d.%d | ISS: %d | OSS: %d | BSS: %d | DMA64: " "%d\n", (int)vmaj, (int)vmin, (int)driver->num_iss, (int)driver->num_oss, (int)driver->num_bss, (int)driver->supports_64bit_dma);

	if (!intel_hda_driver_reset_controller(driver)) {
		return false;
	}

	// discover codecs using STATESTS
	driver->codec_mask = mmio_read16(hda_base + HDA_REG_STATESTS) & 0x7FFF;

	driver->num_codecs = 0;

	for (u8 i = 0; i < HDA_MAX_CODECS; i++) {
		if (driver->codec_mask & (1u << i)) {
			printf("[INTEL HDA]: Detected codec at address %d\n", (int)i);
			driver->num_codecs++;
		}
	}

	if (driver->num_codecs == 0) {
		printf("[INTEL HDA]: No codecs detected.\n");
		return true;
	}

	if (!intel_hda_driver_init_command_dma(driver)) {
		return false;
	}

	// query the vendor/device IDs of all detected codecs
	u8 successful_codecs = 0;

	for (u8 i = 0; i < HDA_MAX_CODECS; i++) {
		if (!(driver->codec_mask & (1u << i))) {
			continue;
		}

		u32 codec_id = 0;

		// root node
		if (!intel_hda_driver_send_verb(driver, i, 0x00, HDA_VERB_GET_PARAMETER, HDA_PARAMETER_VENDOR_ID, &codec_id)) {
			printf("[INTEL HDA]: Failed to query codec %d.\n", (int)i);

			continue;
		}

		u16 vendor_id = (u16)(codec_id >> 16);
		u16 device_id = (u16)(codec_id & 0xFFFF);

		printf("[INTEL HDA]: Codec %d | Vendor: 0x%04x | Device: 0x%04x\n", (int)i, (unsigned int)vendor_id, (unsigned int)device_id);

		// enumerate codec function groups and widgets
		if (!intel_hda_driver_enumerate_codec(driver, i)) {
			printf("[INTEL HDA]: Warning: Codec %d enumeration was unsuccessful.\n", (int)i);
		}

		++successful_codecs;
	}

	if (driver->num_codecs != 0 && successful_codecs == 0) {
		printf("[INTEL HDA]: No codecs responded to Get Parameter.\n");
		return false;
	}

	/*
	 * All codecs have now been enumerated.
	 *
	 * Their widget tables and output route databases
	 * are ready for endpoint selection.
	 *
	 * For now, automatically start one default analog
	 * output on this controller.
	 *
	 * A controller with only digital outputs (such as an NVIDIA GPU) can
	 * legitimately have no analog endpoint. That is not a controller
	 * initialization failure.
	 */
	(void)intel_hda_driver_start_default_analog_output(driver);

	printf("[INTEL HDA]: Controller initialization completed.\n");

	return true;

	return true;
}

/*
 * HDA Specification Rev. 1.0a
 *
 * Section 7.1.2: Node Addressing
 * Section 7.2.1: Root Node
 * Section 7.2.2: Function Groups
 * Section 7.2.3: Widgets
 *
 * Enumeration procedure:
 *
 * 1. Query root node NID 0 for its subordinate node count.
 * 2. Enumerate function group nodes.
 * 3. Query each function group's type.
 * 4. Find Audio Function Groups.
 * 5. Query their subordinate node counts.
 * 6. Enumerate widgets and query their capabilities.
 */
b8 intel_hda_driver_enumerate_codec(intel_hda_driver_t* driver, u8 codec_addr) {
	if (!driver || !driver->command_dma_ready || codec_addr >= HDA_MAX_CODECS) {
		return false;
	}

	// don't accidentally allocate the same table twice
	if (driver->widgets[codec_addr]) {
		printf("[INTEL HDA]: Widget table already exists for codec %d.\n", (int)codec_addr);

		return false;
	}

	// allocate space for all possible NIDs
	size_t widget_table_size = ((size_t)HDA_NODE_ID_MAX + 1) * sizeof(intel_hda_widget_t);

	driver->widgets[codec_addr] = kmalloc(widget_table_size);

	if (!driver->widgets[codec_addr]) {
		printf("[INTEL HDA]: Failed to allocate widget table for codec %d.\n", (int)codec_addr);

		return false;
	}

	// initialize every entry as invalid
	memset(driver->widgets[codec_addr], 0, widget_table_size);

	printf("[INTEL HDA]: Enumerating codec address %d...\n", (int)codec_addr);

	// allocate output path storage for this codec
	size_t path_table_size = sizeof(intel_hda_output_path_t) * HDA_MAX_OUTPUT_PATHS_PER_CODEC;

	driver->output_paths[codec_addr] = kmalloc(path_table_size);

	if (!driver->output_paths[codec_addr]) {
		printf("[INTEL HDA]: Failed to allocate output path table for codec %d.\n", (int)codec_addr);

		return false;
	}

	memset(driver->output_paths[codec_addr], 0, path_table_size);

	driver->output_path_count[codec_addr] = 0;
	driver->output_paths_truncated[codec_addr] = false;

	/*
	 * Enumerate root node function groups, Root node NID is always 0.
	 *
	 * Get Parameter:
	 *   Parameter ID: 0x04
	 *   Subordinate Node Count
	 */
	u32 response = 0;

	if (!intel_hda_get_parameter(driver, codec_addr, 0, HDA_PARAMETER_SUBORDINATE_NODE_COUNT, &response)) {
		printf("[INTEL HDA]: Failed to query root node count.\n");
		return false;
	}

	u8 fg_start = 0;
	u8 fg_count = 0;

	if (!intel_hda_parse_node_count(response, &fg_start, &fg_count)) {
		printf("[INTEL HDA]: Invalid function group range: 0x%08x\n", (unsigned int)response);
		return false;
	}

	printf("[INTEL HDA]: Codec %d has %d function groups starting at NID %d.\n", (int)codec_addr, (int)fg_count, (int)fg_start);

	// enumerate function groups
	b8 found_audio_fg = false;

	u16 fg_end = (u16)fg_start + fg_count;

	for (u16 fg_nid = fg_start; fg_nid < fg_end; fg_nid++) {
		/*
		 * HDA Specification Rev. 1.0a
		 * Section 7.3.4.4, Figure 84, Table 137
		 *
		 * Get Parameter:
		 *   Parameter ID: 0x05
		 *   Function Group Type
		 */
		u32 fg_type_response = 0;

		if (!intel_hda_get_parameter(driver, codec_addr, (u8)fg_nid, HDA_PARAMETER_FUNCTION_GROUP_TYPE, &fg_type_response)) {
			printf("[INTEL HDA]: Failed to query function group NID %d.\n", (int)fg_nid);
			continue;
		}

		u8 fg_type = fg_type_response & 0xFF;

		if (fg_type == HDA_FUNCTION_GROUP_TYPE_AUDIO) {
			printf("[INTEL HDA]: Audio Function Group found at NID %d.\n", (int)fg_nid);
			found_audio_fg = true;
		} else if (fg_type == HDA_FUNCTION_GROUP_TYPE_MODEM) {
			printf("[INTEL HDA]: Modem Function Group found at NID %d.\n", (int)fg_nid);
			continue;
		} else {
			printf("[INTEL HDA]: Other Function Group found at NID %d, type 0x%02x.\n", (int)fg_nid, (unsigned int)fg_type);
			continue;
		}

		/*
		 * Enumerate widgets in the Audio Function Group.
		 *
		 * Get Parameter:
		 *   Parameter ID: 0x04
		 *   Subordinate Node Count
		 */
		u32 widget_count_response = 0;

		if (!intel_hda_get_parameter(driver, codec_addr, (u8)fg_nid, HDA_PARAMETER_SUBORDINATE_NODE_COUNT, &widget_count_response)) {
			printf("[INTEL HDA]: Failed to query widget count for FG %d.\n", (int)fg_nid);
			continue;
		}

		u8 widget_start = 0;
		u8 widget_count = 0;

		if (!intel_hda_parse_node_count(widget_count_response, &widget_start, &widget_count)) {
			printf("[INTEL HDA]: Invalid widget range for FG %d: 0x%08x\n", (int)fg_nid, (unsigned int)widget_count_response);
			continue;
		}

		printf("[INTEL HDA]: Function Group %d contains %d widgets starting at NID %d.\n", (int)fg_nid, (int)widget_count, (int)widget_start);

		// enumerate individual widgets
		u16 widget_end = (u16)widget_start + widget_count;

		for (u16 widget_nid = widget_start; widget_nid < widget_end; widget_nid++) {
			/*
			 * HDA Specification Rev. 1.0a
			 * Section 7.3.4.6, Figure 86, Table 138
			 *
			 * Get Parameter:
			 *   Parameter ID: 0x09
			 *   Audio Widget Capabilities
			 */
			u32 widget_caps = 0;

			if (!intel_hda_get_parameter(driver, codec_addr, (u8)widget_nid, HDA_PARAMETER_AUDIO_WIDGET_CAPS, &widget_caps)) {
				printf("[INTEL HDA]: Failed to query widget NID %d.\n", (int)widget_nid);
				continue;
			}

			/*
			 * HDA Specification Rev. 1.0a
			 * Section 7.3.4.6
			 *
			 * Decode Audio Widget Type.
			 */
			u8 widget_type = (widget_caps & HDA_WIDGET_CAP_TYPE_MASK) >> HDA_WIDGET_CAP_TYPE_SHIFT;

			// store this widget in the codec's persistent widget table
			intel_hda_widget_t* widget = &driver->widgets[codec_addr][widget_nid];

			widget->valid = true;

			widget->nid = (u8)widget_nid;
			widget->type = widget_type;
			widget->capabilities = widget_caps;
			widget->function_group_nid = (u8)fg_nid;

			// query input amplifier capabilities
			if (widget_caps & HDA_WIDGET_CAP_IN_AMP) {
				u32 amp_caps = 0;

				if (intel_hda_get_parameter(driver, codec_addr, (u8)widget_nid, HDA_PARAMETER_INPUT_AMP_CAPS, &amp_caps)) {
					widget->input_amp_caps = amp_caps;
					widget->has_input_amp_caps = true;
				} else {
					printf("[INTEL HDA]: Failed to query input amp caps for NID %d.\n", (int)widget_nid);
				}
			}

			// query output amplifier capabilities
			if (widget_caps & HDA_WIDGET_CAP_OUT_AMP) {
				u32 amp_caps = 0;

				if (intel_hda_get_parameter(driver, codec_addr, (u8)widget_nid, HDA_PARAMETER_OUTPUT_AMP_CAPS, &amp_caps)) {
					widget->output_amp_caps = amp_caps;
					widget->has_output_amp_caps = true;
				} else {
					printf("[INTEL HDA]: Failed to query output amp caps for NID %d.\n", (int)widget_nid);
				}
			}

			// retrieve the complete connection list for this widget, this is needed  for every widget, not just Pin Complex widgets
			if (widget_caps & HDA_WIDGET_CAP_CONN_LIST) {
				widget->connections = kmalloc(HDA_MAX_CONNECTIONS * sizeof(u8));

				if (!widget->connections) {
					printf("[INTEL HDA]: Failed to allocate connection list for NID %d.\n", (int)widget_nid);
					return false;
				}

				memset(widget->connections, 0, HDA_MAX_CONNECTIONS);

				if (!intel_hda_driver_get_connections(driver, codec_addr, (u8)widget_nid, widget->connections, HDA_MAX_CONNECTIONS, &widget->num_connections)) {
					printf("[INTEL HDA]: Failed to cache connections for NID %d.\n", (int)widget_nid);
					return false;
				}
			}

			// only enumerate physical output candidates
			if (widget_type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
				if (!intel_hda_driver_enumerate_pin(driver, codec_addr, (u8)widget_nid, widget_caps)) {
					printf("[INTEL HDA]: Pin enumeration failed for NID %d.\n", (int)widget_nid);
				}
			}
		}
	}

	if (!found_audio_fg) {
		printf("[INTEL HDA]: No Audio Function Groups found.\n");
		return false;
	}

	// discover playback paths for every output-capable pin, this includes analog outputs and digital outputs
	printf("[INTEL HDA]: Searching playback paths for codec %d...\n", (int)codec_addr);

	// build a persistent database of all valid output routes
	intel_hda_driver_discover_output_paths(driver, codec_addr);

	return true;
}

static void intel_hda_driver_run(void* _) {
	while (1) {
		task_yield();
	}
}

IPCIDRIVER* intel_hda_driver_factory(PCI_Device* dev) {
	intel_hda_driver_t* drv = kmalloc(sizeof(intel_hda_driver_t));
	xassert(drv, "Failed to allocate driver!\n");
	if (!drv) {
		return NULL;
	}

	memset(drv, 0, sizeof(intel_hda_driver_t));
	drv->name = "Intel High Definition Audio Driver";
	drv->pci_device = dev;

	// INTERFACE ASSIGN
	drv->init = intel_hda_driver_init_driver;
	drv->run = intel_hda_driver_run;
	return (IPCIDRIVER*)drv;
}