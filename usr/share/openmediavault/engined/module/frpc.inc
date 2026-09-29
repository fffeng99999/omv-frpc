<?php

/**
 * This file is part of OpenMediaVault.
 *
 * @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    ${GITHUB_USER}
 *
 * OpenMediaVault is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenMediaVault is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

namespace Engined\Module;

/**
 * FRP client engine module.
 *
 * Marks the module dirty whenever the settings or a proxy/visitor entry
 * change so that 'Apply changes' re-runs the Salt state (frpc.toml
 * rendering, systemd unit / docker control), and reports the service
 * status to the global Services monitoring (dashboard).
 */
class Frpc extends \OMV\Engine\Module\ServiceAbstract
	implements \OMV\Engine\Notify\IListener, \OMV\Engine\Module\IServiceStatus
{
	public function getName()
	{
		return "frpc";
	}

	public function getStatus()
	{
		$db = \OMV\Config\Database::getInstance();
		$object = $db->get("conf.service.frpc");
		$backend = $object->get("backend");
		if (empty($backend)) {
			$backend = "native";
		}
		$running = false;
		if ("native" === $backend) {
			$systemCtl = new \OMV\System\SystemCtl("frpc");
			$running = $systemCtl->isActive();
		} else {
			$container = $object->get("dockerContainer");
			if (empty($container)) {
				$container = "frpc";
			}
			$cmd = new \OMV\System\Process("/usr/sbin/omv-frpc-ctl", [
				"is-active", "docker", $container
			]);
			$cmd->setRedirect2to1();
			$cmd->setQuiet(true);
			$output = [];
			$exitStatus = 0;
			$cmd->execute($output, $exitStatus);
			$running = (trim(implode("\n", $output)) === "running");
		}
		return [
			"name" => $this->getName(),
			"title" => gettext("FRP Client"),
			"enabled" => $object->get("enable"),
			"running" => $running
		];
	}

	public function bindListeners(\OMV\Engine\Notify\Dispatcher $dispatcher)
	{
		$dispatcher->addListener(
			OMV_NOTIFY_MODIFY,
			"org.openmediavault.conf.service.frpc",
			[$this, "setDirty"]
		);
		$dispatcher->addListener(
			OMV_NOTIFY_CREATE,
			"org.openmediavault.conf.service.frpc.proxy",
			[$this, "setDirty"]
		);
		$dispatcher->addListener(
			OMV_NOTIFY_MODIFY,
			"org.openmediavault.conf.service.frpc.proxy",
			[$this, "setDirty"]
		);
		$dispatcher->addListener(
			OMV_NOTIFY_DELETE,
			"org.openmediavault.conf.service.frpc.proxy",
			[$this, "setDirty"]
		);
	}
}
