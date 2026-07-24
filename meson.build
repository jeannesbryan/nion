project(
  'nion',
  'c',
  version: '1.0.0',
  meson_version: '>= 0.61.0',
  default_options: [
    'c_std=c17',
    'warning_level=3',
    'buildtype=release',
  ],
)

gtk_dep = dependency('gtk4', version: '>= 4.10')
webkit_dep = dependency('webkitgtk-6.0', version: '>= 2.40')
gio_dep = dependency('gio-2.0')
soup_dep = dependency('libsoup-3.0')
gnome = import('gnome')

nion_resources = gnome.compile_resources(
  'nion-resources',
  'data/nion.gresource.xml',
  source_dir: 'data',
  c_name: 'nion',
)

conf = configuration_data()
conf.set_quoted('NION_VERSION', meson.project_version())
configure_file(output: 'config.h', configuration: conf)

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
  'data/io.github.jeannesbryan.Nion.metainfo.xml',
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
