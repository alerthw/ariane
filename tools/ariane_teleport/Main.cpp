#include <plugin.h>
#include <CPlayerPed.h>
#include <CWorld.h>
#include <cstdio>

using namespace plugin;

class ArianeTeleport {
public:
	ArianeTeleport() {
		Events::gameProcessEvent += [] {
			static bool done = false;
			if(done) return;

			CPlayerPed *player = FindPlayerPed();
			if(!player) return;  // player not spawned yet, retry next frame

			FILE *f = fopen("ariane_teleport.txt", "r");
			if(!f){ done = true; return; }

			float x, y, z, heading;
			if(fscanf(f, "%f %f %f %f", &x, &y, &z, &heading) == 4){
				fclose(f);
				remove("ariane_teleport.txt");
#if defined(GTASA)
				player->Teleport(CVector(x, y, z), false);
#else
				player->Teleport(CVector(x, y, z));
#endif
				player->SetHeading(heading);
			} else {
				fclose(f);
				remove("ariane_teleport.txt");
			}
			done = true;
		};
	}
} arianeTeleportInstance;
