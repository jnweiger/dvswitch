include(Symlink)

file(GLOB pofiles *.po)

set(CMAKE_INSTALL_PREFIX $ENV{DESTDIR}/${prefix})

add_custom_target(
  update-po
  COMMAND xgettext -p po -o dvswitch.pot --package-name=dvswitch
          --package-version=${PACKAGE_VERSION}
          --msgid-bugs-address=dvswitch-devel@lists.alioth.debian.org
          src/*.c src/*.cpp
  COMMAND sed -i "'1,4c\\
# DVswitch\\
# Copyright (C) 2007-2010 Ben Hutchings et al.\\
# This file is distributed under the same license as the DVswitch package.'"
          po/dvswitch.pot
  COMMAND for po in ${pofiles}\; do msgmerge -U $$po po/dvswitch.pot\; done
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )

GETTEXT_CREATE_TRANSLATIONS(dvswitch.pot ALL ${pofiles})
