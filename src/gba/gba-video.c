#include "gba-video.h"

#include "gba.h"
#include "gba-io.h"
#include "gba-thread.h"

#include <limits.h>
#include <string.h>
#include <sys/mman.h>

static void GBAVideoDummyRendererInit(struct GBAVideoRenderer* renderer);
static void GBAVideoDummyRendererDeinit(struct GBAVideoRenderer* renderer);
static uint16_t GBAVideoDummyRendererWriteVideoRegister(struct GBAVideoRenderer* renderer, uint32_t address, uint16_t value);
static void GBAVideoDummyRendererDrawScanline(struct GBAVideoRenderer* renderer, int y);
static void GBAVideoDummyRendererFinishFrame(struct GBAVideoRenderer* renderer);

static struct GBAVideoRenderer dummyRenderer = {
	.init = GBAVideoDummyRendererInit,
	.deinit = GBAVideoDummyRendererDeinit,
	.writeVideoRegister = GBAVideoDummyRendererWriteVideoRegister,
	.drawScanline = GBAVideoDummyRendererDrawScanline,
	.finishFrame = GBAVideoDummyRendererFinishFrame
};

void GBAVideoInit(struct GBAVideo* video) {
	video->renderer = &dummyRenderer;

	video->inHblank = 0;
	video->inVblank = 0;
	video->vcounter = 0;
	video->vblankIRQ = 0;
	video->hblankIRQ = 0;
	video->vcounterIRQ = 0;
	video->vcountSetting = 0;

	video->vcount = -1;

	video->lastHblank = 0;
	video->nextHblank = VIDEO_HDRAW_LENGTH;
	video->nextEvent = video->nextHblank;
	video->eventDiff = video->nextEvent;

	video->nextHblankIRQ = 0;
	video->nextVblankIRQ = 0;
	video->nextVcounterIRQ = 0;

	video->vram = mmap(0, SIZE_VRAM, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
}

void GBAVideoDeinit(struct GBAVideo* video) {
	GBAVideoAssociateRenderer(video, &dummyRenderer);
	munmap(video->vram, SIZE_VRAM);
}

void GBAVideoAssociateRenderer(struct GBAVideo* video, struct GBAVideoRenderer* renderer) {
	video->renderer->deinit(video->renderer);
	video->renderer = renderer;
	renderer->palette = video->palette;
	renderer->vram = video->vram;
	renderer->oam = &video->oam;
	video->renderer->init(video->renderer);
}

int32_t GBAVideoProcessEvents(struct GBAVideo* video, int32_t cycles) {
	video->nextEvent -= cycles;
	if (video->nextEvent <= 0) {
		video->lastHblank -= video->eventDiff;
		video->nextHblank -= video->eventDiff;
		video->nextHblankIRQ -= video->eventDiff;
		video->nextVcounterIRQ -= video->eventDiff;

		if (video->inHblank) {
			// End Hblank
			video->inHblank = 0;
			video->nextEvent = video->nextHblank;

			++video->vcount;
			video->p->memory.io[REG_VCOUNT >> 1] = video->vcount;

			switch (video->vcount) {
			case VIDEO_VERTICAL_PIXELS:
				video->inVblank = 1;
				if (GBASyncDrawingFrame(video->p->sync)) {
					video->renderer->finishFrame(video->renderer);
				}
				video->nextVblankIRQ = video->nextEvent + VIDEO_TOTAL_LENGTH;
				GBAMemoryRunVblankDMAs(&video->p->memory);
				if (video->vblankIRQ) {
					GBARaiseIRQ(video->p, IRQ_VBLANK);
				}
				GBASyncPostFrame(video->p->sync);
				break;
			case VIDEO_VERTICAL_TOTAL_PIXELS - 1:
				video->inVblank = 0;
				break;
			case VIDEO_VERTICAL_TOTAL_PIXELS:
				video->vcount = 0;
				video->p->memory.io[REG_VCOUNT >> 1] = 0;
				break;
			}

			video->vcounter = video->vcount == video->vcountSetting;
			if (video->vcounter && video->vcounterIRQ) {
				GBARaiseIRQ(video->p, IRQ_VCOUNTER);
				video->nextVcounterIRQ += VIDEO_TOTAL_LENGTH;
			}

			if (video->vcount < VIDEO_VERTICAL_PIXELS && GBASyncDrawingFrame(video->p->sync)) {
				video->renderer->drawScanline(video->renderer, video->vcount);
			}
		} else {
			// Begin Hblank
			video->inHblank = 1;
			video->lastHblank = video->nextHblank;
			video->nextEvent = video->lastHblank + VIDEO_HBLANK_LENGTH;
			video->nextHblank = video->nextEvent + VIDEO_HDRAW_LENGTH;
			video->nextHblankIRQ = video->nextHblank;

			if (video->vcount < VIDEO_VERTICAL_PIXELS) {
				GBAMemoryRunHblankDMAs(&video->p->memory);
			}
			if (video->hblankIRQ) {
				GBARaiseIRQ(video->p, IRQ_HBLANK);
			}
		}

		video->eventDiff = video->nextEvent;
	}
	return video->nextEvent;
}

void GBAVideoWriteDISPSTAT(struct GBAVideo* video, uint16_t value) {
	video->vblankIRQ = value & 0x0008;
	video->hblankIRQ = value & 0x0010;
	video->vcounterIRQ = value & 0x0020;
	video->vcountSetting = (value & 0xFF00) >> 8;

	if (video->vcounterIRQ) {
		// FIXME: this can be too late if we're in the middle of an Hblank
		video->nextVcounterIRQ = video->nextHblank + VIDEO_HBLANK_LENGTH + (video->vcountSetting - video->vcount) * VIDEO_HORIZONTAL_LENGTH;
		if (video->nextVcounterIRQ < video->nextEvent) {
			video->nextVcounterIRQ += VIDEO_TOTAL_LENGTH;
		}
	}
}

uint16_t GBAVideoReadDISPSTAT(struct GBAVideo* video) {
	return (video->inVblank) | (video->inHblank << 1) | (video->vcounter << 2);
}

static void GBAVideoDummyRendererInit(struct GBAVideoRenderer* renderer) {
	(void)(renderer);
	// Nothing to do
}

static void GBAVideoDummyRendererDeinit(struct GBAVideoRenderer* renderer) {
	(void)(renderer);
	// Nothing to do
}

static uint16_t GBAVideoDummyRendererWriteVideoRegister(struct GBAVideoRenderer* renderer, uint32_t address, uint16_t value) {
	(void)(renderer);
	(void)(address);
	return value;
}

static void GBAVideoDummyRendererDrawScanline(struct GBAVideoRenderer* renderer, int y) {
	(void)(renderer);
	(void)(y);
	// Nothing to do
}

static void GBAVideoDummyRendererFinishFrame(struct GBAVideoRenderer* renderer) {
	(void)(renderer);
	// Nothing to do
}
