HHVM_DEFINE_EXTENSION("gettext"
  SOURCES
    ext_gettext.cpp
  HEADERS
    ext_gettext.h
  SYSTEMLIB
    ext_gettext.php
  DEPENDS
    libIntl
)