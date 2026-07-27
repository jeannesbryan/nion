project(
  'nion',
  'c',
  version: files('release/manifest/NION_VERSION'),
  meson_version: '>= 0.61.0',
  default_options: [
    'c_std=c17',
    'warning_level=3',
    'buildtype=release',
  ],
)

fs = import('fs')
gnome = import('gnome')

release_status = fs.read('release/manifest/RELEASE_STATUS').strip()
appstream_release_type = fs.read('release/manifest/APPSTREAM_RELEASE_TYPE').strip()
tor_browser_version = fs.read('release/manifest/TOR_BROWSER_VERSION').strip()
tor_daemon_version = fs.read('release/manifest/TOR_DAEMON_VERSION').strip()
gtk_min_version = fs.read('release/manifest/GTK_MIN_VERSION').strip()
webkitgtk_min_version = fs.read('release/manifest/WEBKITGTK_MIN_VERSION').strip()
gtk_tested_version = fs.read('release/manifest/GTK_TESTED_VERSION').strip()
webkitgtk_tested_version = fs.read('release/manifest/WEBKITGTK_TESTED_VERSION').strip()
glib_tested_version = fs.read('release/manifest/GLIB_TESTED_VERSION').strip()

gtk_dep = dependency('gtk4', version: '>=' + gtk_min_version)
webkit_dep = dependency('webkitgtk-6.0', version: '>=' + webkitgtk_min_version)
gio_dep = dependency('gio-2.0')
soup_dep = dependency('libsoup-3.0')

nion_resources = gnome.compile_resources(
  'nion-resources',
  'data/nion.gresource.xml',
  source_dir: 'data',
  c_name: 'nion',
)

conf = configuration_data()
conf.set_quoted('NION_VERSION', meson.project_version())
conf.set_quoted('NION_RELEASE_STATUS', release_status)
conf.set_quoted('NION_TOR_BROWSER_BUNDLE_VERSION', tor_browser_version)
conf.set_quoted('NION_TOR_DAEMON_VERSION', tor_daemon_version)
conf.set_quoted('NION_GTK_TESTED_VERSION', gtk_tested_version)
conf.set_quoted('NION_WEBKITGTK_TESTED_VERSION', webkitgtk_tested_version)
conf.set_quoted('NION_GLIB_TESTED_VERSION', glib_tested_version)
configure_file(output: 'config.h', configuration: conf)

metainfo_conf = configuration_data()
metainfo_conf.set('NION_VERSION', meson.project_version())
metainfo_conf.set('APPSTREAM_RELEASE_TYPE', appstream_release_type)
nion_metainfo = configure_file(
  input: 'data/io.github.jeannesbryan.Nion.metainfo.xml.in',
  output: 'io.github.jeannesbryan.Nion.metainfo.xml',
  configuration: metainfo_conf,
)

executable(
  'nion',
  'src/main.c',
  nion_resources,
  include_directories: include_directories('.'),
  dependencies: [gtk_dep, webkit_dep, gio_dep, soup_dep],
  install: true,
)

install_data(
  'data/io.github.jeannesbryan.Nion.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'data/nion.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
  rename: 'io.github.jeannesbryan.Nion.svg',
)

install_data(
  nion_metainfo,
  install_dir: get_option('datadir') / 'metainfo',
)

install_data(
  'LICENSE',
  install_dir: get_option('datadir') / 'licenses' / 'nion',
)

install_data(
  'THIRD_PARTY_NOTICES.md',
  install_dir: get_option('datadir') / 'doc' / 'nion',
)
