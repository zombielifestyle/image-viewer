# Image Viewer

Simple image viewer using glfw, GL.

Learning Project, may be broken at any time.

## Build

./vendorize.sh can pull deps. Untested with great care.

	`make run`

## Usage
	./image-viewer [<options>] [<files>]
	-w   width (pixels or %)
	-h   height (pixels or %)

## Controls
 * `R`: toggle fitted mode
 * `C`: center image
 * `S`: toggle wave shader for lulz
 * `SPACE`: panning
 * `MOUSE WHEEL`: zooming
 * `RMB` move window
 * `F`, `F11`, `LMB` double click: maximize / restore
 * `LEFT` / `RIGHT`: next image (not really implemented yet)

Image formats:
 * jpeg
 * png
 * more soon (TM)

## AI Disclosure

I currently don't use coding agents. I use AI chatbots for research, learning and code snippets.
The presence of AI code is relevant, incompetence of AI as well. Code snippets are refactored / restructured / rewritten to make sense.
