include(Symlink)

file(GLOB pofiles *.po)

set(CMAKE_INSTALL_PREFIX $ENV{DESTDIR}/${prefix})

# TODO: add a target to build dvswitch.pot, like so:
# xgettext -j -p . -n --package-name=dvswitch --package-version=0.9 -D ../src -o dvswitch.pot mixer_window.cpp

GETTEXT_CREATE_TRANSLATIONS(dvswitch.pot ALL ${pofiles})
