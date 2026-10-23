# Local mx5000tools patch

`mx5000tools-hidraw-diagnostics.patch` records the difference found
between the upstream `libmx5000/hidraw_lib.c` and the locally modified
working copy.

The modification was made during troubleshooting when the LCD showed
no image. It adds diagnostic output around HID writes and comments out
the original ENODEV recovery block.

This patch is preserved for review and reproducibility. It has not
been declared a generally correct upstream fix.

Review it before applying it to another version of mx5000tools.
