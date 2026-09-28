# This file is part of OpenMediaVault.
#
# @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
# @author    ${GITHUB_USER}
#
# OpenMediaVault is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.

# Renders frpc.toml from the OMV configuration database.
#
# The native backend uses the bundled /usr/bin/frpc binary and the
# generated frpc.service systemd unit. The docker backend only renders
# the configuration file bind-mounted into an existing frpc container
# and starts/restarts/stops that container through omv-frpc-ctl; the
# plugin never creates or removes containers and never edits compose
# stack files.

{% set config = salt['omv_conf.get']('conf.service.frpc') %}
{% set backend = config.backend | default('docker') %}
{% if backend == 'native' %}
{% set target_path = '/etc/frp/frpc.toml' %}
{% else %}
{% set target_path = config.dockerConfigPath | default('/etc/frp/frpc.toml') %}
{% endif %}

# Before the first takeover, keep a one-time backup of an existing
# frpc.toml (e.g. a file that used to be edited by hand) next to it.
backup_existing_frpc_config:
  cmd.run:
    - name: cp -- '{{ target_path }}' '{{ target_path }}.omv-bak'
    - onlyif: test -f '{{ target_path }}'
    - unless: test -f '{{ target_path }}.omv-bak'

# Always render the configuration for the active backend (also when the
# service is disabled, so the preview/reload actions have a file).
render_frpc_config:
  file.managed:
    - name: "{{ target_path }}"
    - source:
      - salt://{{ tpldir }}/files/frpc.toml.j2
    - template: jinja
    - context:
        config: {{ config | json }}
    - user: root
    - group: root
    - mode: '0600'
    - makedirs: True

{% if backend == 'native' %}

{% if config.enable | to_bool %}

create_frpc_systemd_unit_file:
  file.managed:
    - name: /etc/systemd/system/frpc.service
    - source:
      - salt://{{ tpldir }}/files/frpc.service.j2
    - template: jinja
    - user: root
    - group: root
    - mode: '0644'

frpc_systemctl_daemon_reload:
  module.run:
    - service.systemctl_reload:
    - onchanges:
      - file: create_frpc_systemd_unit_file

start_frpc_service:
  service.running:
    - name: frpc
    - enable: True
    - watch:
      - file: render_frpc_config
      - file: create_frpc_systemd_unit_file

{% else %}

# The native unit only exists while the service is enabled. Salt
# requires every state ID to be unique inside one rendered SLS, so the
# enabled and disabled branches must not share state IDs.
stop_frpc_service:
  service.dead:
    - name: frpc
    - enable: False
    - onlyif: test -f /etc/systemd/system/frpc.service

remove_frpc_systemd_unit_file:
  file.absent:
    - name: /etc/systemd/system/frpc.service

frpc_systemctl_daemon_reload:
  module.run:
    - service.systemctl_reload:
    - onchanges:
      - file: remove_frpc_systemd_unit_file

{% endif %}

{% else %}

# Docker backend: make sure no native unit is left over from a backend
# switch to avoid two frpc instances running at the same time.
remove_native_frpc_systemd_unit_file:
  file.absent:
    - name: /etc/systemd/system/frpc.service

frpc_systemctl_daemon_reload:
  module.run:
    - service.systemctl_reload:
    - onchanges:
      - file: remove_native_frpc_systemd_unit_file

{% set container = config.dockerContainer | default('frpc') | replace("'", "") %}
{% set container_cfg = config.dockerContainerConfigPath | default('/etc/frp/frpc.toml') | replace("'", "") %}

{% if config.enable | to_bool %}

start_frpc_container:
  cmd.run:
    - name: /usr/sbin/omv-frpc-ctl start docker '{{ container }}' '{{ container_cfg }}'
    - require:
      - file: render_frpc_config

# Pick up the new configuration without requiring a manual reload.
restart_frpc_container_on_change:
  cmd.run:
    - name: /usr/sbin/omv-frpc-ctl restart docker '{{ container }}' '{{ container_cfg }}'
    - onchanges:
      - file: render_frpc_config
    - require:
      - cmd: start_frpc_container

{% else %}

stop_frpc_container:
  cmd.run:
    - name: /usr/sbin/omv-frpc-ctl stop docker '{{ container }}' '{{ container_cfg }}'

{% endif %}

{% endif %}
