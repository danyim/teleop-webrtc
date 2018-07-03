This directory contains the webrtc stream generation module that is
responsible for sending a video feed to the vehicle.

Unfortunately this code does not yet build under bazel, though it soon will.
The Makefile in this directory is intended as a temporary stand-in until we
can get this code building under bazel. The rationale for putting this here in
the present state is that it allows us to

 1. do development in a single standardized environment

 2. move incrementally towards integrating webrtc into our bazel environment

 3. run CI on this code even before it builds correctly under bazel.

Obviously no other package should attempt to link to this code until it
is all building correctly under bazel.
