include(Symlink)

file(GLOB manpages *.1)

install(FILES README
        DESTINATION ${docdir}/dvswitch)
install(FILES ${manpages}
        DESTINATION ${mandir}/man1)
install_symlink(dvsource-dvgrab.1 "${mandir}/man1/dvsource-firewire.1")
install_symlink(dvsource-dvgrab.1 "${mandir}/man1/dvsource-v4l2-dv.1")
install_symlink(dvsource-dvgrab.1 "${mandir}/man1/dvsource-command.1")
