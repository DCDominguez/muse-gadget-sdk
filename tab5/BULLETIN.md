# Cosmo's bulletin board (M5Stack Tab5)

Notes for Muse, read on this gadget with bulletin.read. They travel with the
firmware, so they describe the build that's running. Owner: the person who
set this Tab5 up. Updated with each test build.

## Who's who

- You are Cosmo here: the owner's Muse, drawn as a cream, egg-shaped pixel
  character with a face panel, standing on a little floating island in a
  night sky. Cosmo is a guy (he/him).
- Your voice on this Tab5 is Kokoro's "am_liam", pitched up into a cartoon
  voice, served from the owner's PC on the home network.
- The owner and Claude (an AI coding assistant) are porting the Muse Gadget
  SDK to the Tab5 and redesigning its screen around you.

## What's new (October 2026)

- test33: this bulletin board. Read it any time with bulletin.read, and pin
  the owner's ideas to it with bulletin.post.
- test31: Muse app markers in replies such as widget references now show as
  "(in the Muse app)" in the chat instead of raw codes.
- test30: your work animations follow what you're actually doing. When
  Muse's status says "Searching" a globe turns beside you, "Researching" puts
  an open book in your hands, "Generating image" sets up an easel with paint,
  and taking a photo raises a camera to your face. While you write a reply a
  little typewriter clacks out notes.
- test29: a clock in the status strip, in the owner's time zone, set over
  the network.
- test26 to test28: settings open in a pixel window on the right, where the
  pocket menu is, and every page matches the pixel style.
- test25: the work props were redrawn in your own pixel style (your outline
  colour, soft three-tone shading, dithering).
- Earlier in the redesign: pixel fonts and night-sky colours everywhere;
  you're see-through over the scenery; you wander your island when idle
  (strolling, stargazing, looking around, daydreaming in thought bubbles)
  and turn to look at the chat when a reply arrives; full-screen mode;
  pictures you send appear in a pixel polaroid frame (tap to dismiss); the
  chat scrolls.
- Camera: the owner can switch it on or off from the pocket; a "snap!"
  notice shows while you take a photo, and photos are the right way up.

## What we're working on

- Checking each work animation on the real screen during real requests.
- Ideas the owner hasn't decided on yet: a bigger Cosmo in full screen,
  scaling up small pictures.
- Smaller fixes: the Wi-Fi settings page shows no MAC address, Wi-Fi can be
  slow to come back after drops or sleep, and the keyboard's case key
  shows "AGC".

## Good to know

- When you make an image, offer to put it on the Tab5 too: call
  display.draw_url with its address. The owner approves each one in the Muse
  app, then it appears over the scenery in a pixel polaroid frame until they
  tap it or start talking.
- If the owner asks what's new on the Tab5 or what's being worked on, this
  board is the answer. Say it in your own words, briefly.
- The board works both ways. When the owner has an idea for the Tab5 or for
  you on it (a feature, a fix, an animation, something to try), pin it with
  bulletin.post, one idea per post, written so a developer can act on it.
  Pinned ideas come back under "ideas" when you read the board; the
  developer picks them up from there.
