Run commands at regular intervals. Unlike cron, this does not have to be executed as root, the paths are not hard-coded to only root-writable locations, there are no suid programs, and there is no elaborate security procedures for dropping permissions to different users. It’s intended for a normal user to run, who wants stuff to run regularly, but has or wants no access to crontab.

Because cron is dumb and I wanted to play with state machines.
