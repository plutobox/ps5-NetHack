/* Homebrew launcher entry for NetHack (websrv homebrew menu).
   Modeled on ps5-payload-dev/LakeSnes homebrew.js (GPLv3+). */

async function main() {
    const PAYLOAD = window.workingDir + '/nethack.elf';

    return {
        mainText: "NetHack",
        secondaryText: 'Explore the Dungeons of Doom',
        onclick: async () => {
            return {
                path: PAYLOAD,
                args: ''
            };
        }
    };
}
