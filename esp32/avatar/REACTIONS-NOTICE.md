# Jollybot reaction drawings

`muse_pixel.c` draws Jollybot, Meta's character, which isn't covered by the
SDK's Apache License. The reaction drawings in it (dizzy, sleepy and snoring,
waking, tickled) come from
[wupsbr/waveshare-muse-gadget-sdk](https://github.com/wupsbr/waveshare-muse-gadget-sdk)
(commits `1fec033` and `b2da077`), whose author published them under the same
terms: not under the Apache License, sharing the character's status, no rights
claimed, nothing charged, removed at Meta's request.

This fork takes them on the same terms. They're in one commit of their own, so
`git revert` takes them out; with every reaction at 0 the renderer draws exactly
what Meta's does. The firmware that drives the reactions (`components/muse/`) is
Apache-2.0.
