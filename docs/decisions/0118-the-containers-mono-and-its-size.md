# 0118. The Container's Mono, and its size

## Context

Asked (22 November): a Mono choice under the Container's Stereo drop-down;
and the Container should check its scaling when a plug-in is added or another
is chosen by clicking its name. It was made 1000 by 300 once: a plug-in
shown inside it bigger than what was left was cut off, and the rack fitted
the Container's old size, not what it showed.

## Decision

- **Mono**, under Stereo, Mid and Side: the plug-in hears both sides as one
  (half of each) and what it sends back is one, on both sides; nothing passes
  it by but what's outside its band (Mid lets the sides by). It's a parameter
  of its own per place, `sN_mono`, made after every parameter there was:
  choices are saved as fractions, so a fourth would have turned saved Mid into
  Side and Side into Mono; and automation finds parameters by number.
  Choosing Stereo, Mid or Side turns it off. The button says Mono.
- **The Container takes the plug-in's size**: the list beside the plug-in
  shown, as big as that plug-in, when it's shown (chosen by its name), when
  one is added (and it's the one shown), and when the plug-in resizes itself.
  The rack fits whatever size the Container is (0058).

## Consequences

Tested: on Mono, a plug-in that changes nothing turns the left alone into a
quarter on each side; one that silences leaves nothing; every earlier
parameter is where it was and Side saved as before is still Side; a plug-in
added, or chosen by name, or grown, is inside the Container whole. Also found
and fixed: a window retired by 0115 kept its plug-in window's size rules
and was asked for them after they'd gone (the Linux tests crashed).
