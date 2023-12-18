chroot /mnt/slack_x2mount /usr/bin/env -i PS1="(CHROOT) $PS1" LANG=C \
    PATH="$PATH" HOME="/root" USER="root" TERM="linux" LC_ALL=C /bin/bash