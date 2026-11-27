// What the chip's own bootloader can tell, and writing the firmware, with esptool-js and no dialog of its own.

import { ESPLoader, Transport } from './vendor/esptool.js';
import { md5 } from './md5.js';

/* a port that does not let go must not hold the page for ever */
const within = (promise, ms) => Promise.race([promise, new Promise((r) => {
  setTimeout(() => r(false), ms);
})]);

const wait = (ms) => new Promise((r) => {
  setTimeout(r, ms);
});

/* the reset itself, as esptool does it: DTR down first, so the BOOT pin is up when the chip wakes. with
 * it still down the chip wakes in its bootloader, the screen stays black, and nothing answers. the
 * library's own after() asks the chip about its USB first, and on the C3 no answer came */
async function restart(transport) {
  await transport.setDTR(false);
  await transport.setRTS(true);
  await wait(200);
  await transport.setRTS(false);
  await wait(100);
}

async function letGo(port, transport) {
  await within(transport.disconnect().catch((e) => console.warn(e)), 3000);
  if (port.readable) {
    await within(port.close().catch((e) => console.warn(e)), 2000);
  }
}

const FAMILY = { ESP32: 'esp32', 'ESP32-S3': 's3', 'ESP32-C3': 'c3', 'ESP32-C5': 'c5', 'ESP32-P4': 'p4' };

function terminal(say) {
  return {
    clean() {
    },
    writeLine(text) {
      say(text);
    },
    write(text) {
      say(text);
    },
  };
}

async function loader(port, say) {
  const transport = new Transport(port, false);
  const esp = new ESPLoader({ transport, baudrate: 460800, romBaudrate: 115200, terminal: terminal(say) });
  try {
    await esp.main();
  } catch (e) {
    // the tries left the chip in its bootloader: dark, and dark until a restart. its own firmware again
    await within(restart(transport).catch((x) => console.warn(x)), 2000);
    await letGo(port, transport);                      // a chip that did not answer must not keep the port
    throw e;
  }
  return { transport, esp };
}

/* the chip behind a port with no Flight Radar on it: { chip, name, mac } */
export async function detect(port, say) {
  const { transport, esp } = await loader(port, say);
  try {
    const name = esp.chip.CHIP_NAME;
    const mac = (await esp.chip.readMac(esp)).replaceAll(':', '');
    await restart(transport);
    return { chip: FAMILY[name] ?? null, name, mac };
  } finally {
    await letGo(port, transport);
  }
}

/* the build's parts and the chip it is for (the manifest's chipFamily, as esptool names chips: ESP32, ESP32-S3..) */
async function parts(dir) {
  const got = await fetch(`${dir}/manifest.json`);
  if (!got.ok) {                                       // a board the site has no build of: say so, not "Unexpected token <"
    throw new Error(`${dir}/manifest.json: the site answered ${got.status}`);
  }
  const build = (await got.json()).builds[0];
  const fileArray = await Promise.all(build.parts.map(async (p) => {
    const answer = await fetch(`${dir}/${p.path}`);
    if (!answer.ok) {
      throw new Error(`${p.path}: the site answered ${answer.status}`);
    }
    return { data: new Uint8Array(await answer.arrayBuffer()), address: p.offset };
  }));
  return { fileArray, family: build.chipFamily };
}

/* dir: the board's folder of the version, fw/<board> for the stable, fw/<version>/<board> for the beta.
 * erase: everything goes, for a board that had something else on it. settings stay without it */
export async function flash(port, dir, erase, progress, say) {
  const { fileArray, family } = await parts(dir);
  const total = fileArray.reduce((sum, f) => sum + f.data.length, 0);
  const { transport, esp } = await loader(port, say);
  try {
    /* the build must be for the chip on the cable: picked from "show all boards", an S3 build went into an ESP32,
     * the MD5 matched what was written and the board stayed dark (the 2026-11-27 review). nothing is written */
    const chip = esp.chip.CHIP_NAME;
    if (family && chip !== family) {
      await restart(transport);
      throw new Error(`this build is for an ${family}, the board on this cable is an ${chip}: nothing was written`);
    }
    let before = 0;
    await esp.writeFlash({
      fileArray,
      flashMode: 'keep',
      flashFreq: 'keep',
      flashSize: 'keep',
      eraseAll: erase,
      compress: true,
      calculateMD5Hash: md5,                           // the chip's hash of what it wrote must match
      reportProgress(index, written, size) {
        const done = fileArray.slice(0, index).reduce((sum, f) => sum + f.data.length, 0);
        before = done + (written / size) * fileArray[index].data.length;
        progress(Math.min(99, Math.round(before / total * 100)));
      },
    });
    progress(100);
    await restart(transport);
  } finally {
    await letGo(port, transport);
  }
}
