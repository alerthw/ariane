#include "euryopa.h"

CPtrList instances;
CPtrList selection;

void
ObjectInst::UpdateMatrix(void)
{
	m_matrix.rotate(conj(m_rotation), rw::COMBINEREPLACE);
	m_matrix.translate(&m_translation, rw::COMBINEPOSTCONCAT);
}

void*
ObjectInst::CreateRwObject(void)
{
	rw::Frame *f;
	rw::Atomic *atomic;
	rw::Clump *clump;
	ObjectDef *obj = GetObjectDef(m_objectId);

	if(obj->m_type == ObjectDef::ATOMIC){
		if(obj->m_atomics[0] == nil)
			return nil;
		atomic = obj->m_atomics[0]->clone();
		f = rw::Frame::create();
		atomic->setFrame(f);
		f->transform(&m_matrix, rw::COMBINEREPLACE);
		m_rwObject = atomic;
	}else if(obj->m_type == ObjectDef::CLUMP){
		if(obj->m_clump == nil)
			return nil;
		clump = obj->m_clump->clone();
		f = clump->getFrame();
		f->transform(&m_matrix, rw::COMBINEREPLACE);
		m_rwObject = clump;
	}
	return m_rwObject;
}

void
ObjectInst::Init(FileObjectInstance *fi)
{
	m_objectId = fi->objectId;
	if(fi->area & 0x100) m_isUnimportant = true;
	if(fi->area & 0x400) m_isUnderWater = true;
	if(fi->area & 0x800) m_isTunnel = true;
	if(fi->area & 0x1000) m_isTunnelTransition = true;
	m_area = fi->area & 0xFF;
	m_rotation = fi->rotation;
	m_translation = fi->position;
	m_lodId = fi->lod;
	UpdateMatrix();
}

void
ObjectInst::SetupBigBuilding(void)
{
	m_isBigBuilding = true;
}

CRect
ObjectInst::GetBoundRect(void)
{
	CRect rect;
	rw::V3d v;
	CColModel *col = GetObjectDef(m_objectId)->m_colModel;

	v = col->boundingBox.min;
	rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
	rect.ContainPoint(v);

	v = col->boundingBox.max;
	rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
	rect.ContainPoint(v);

	v = col->boundingBox.min;
	v.x = col->boundingBox.max.x;
	rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
	rect.ContainPoint(v);

	v = col->boundingBox.max;
	v.x = col->boundingBox.min.x;
	rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
	rect.ContainPoint(v);

	return rect;
}

bool
ObjectInst::IsOnScreen(void)
{
	rw::Sphere sph;
	ObjectDef *obj = GetObjectDef(m_objectId);
	CColModel *col = obj->m_colModel;
	sph.center = col->boundingSphere.center;
	sph.radius = col->boundingSphere.radius;
	return TheCamera.IsSphereVisible(&sph, &m_matrix);
}

static void
updateMatFXAnim(rw::Material *m)
{
	if(!rw::UVAnim::exists(m))
		return;
	rw::UVAnim::addTime(m, timeStep);
	rw::UVAnim::applyUpdate(m);
}

void
ObjectInst::PreRender(void)
{
	int i;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj->m_hasPreRendered)
		return;
	obj->m_hasPreRendered = true;

	if(obj->m_type == ObjectDef::ATOMIC && gPlayAnimations){
		rw::Atomic *atm = (rw::Atomic*)m_rwObject;
		if(rw::MatFX::getEffects(atm))
			for(i = 0; i < atm->geometry->matList.numMaterials; i++)
				updateMatFXAnim(atm->geometry->matList.materials[i]);
	}
}

void
ObjectInst::JumpTo(void)
{
	rw::V3d center;
	ObjectDef *obj = GetObjectDef(m_objectId);
	CSphere *sph = &obj->m_colModel->boundingSphere;
	rw::V3d::transformPoints(&center, &sph->center, 1, &m_matrix);
	TheCamera.setTarget(center);
	TheCamera.setDistanceFromTarget(TheCamera.minDistToSphere(sph->radius));
}

void
ObjectInst::Select(void)
{
	if(m_selected) return;
	m_selected = true;
	selection.InsertItem(this);
}

void
ObjectInst::Deselect(void)
{
	CPtrNode *p;
	if(!m_selected) return;
	m_selected = false;
	for(p = selection.first; p; p = p->next)
		if(p->item == this){
			selection.RemoveNode(p);
			return;
		}
}

void
ClearSelection(void)
{
	CPtrNode *p, *next;
	for(p = selection.first; p; p = next){
		next = p->next;
		((ObjectInst*)p->item)->m_selected = false;
		selection.RemoveNode(p);
	}
}

void
ObjectInst::Delete(void)
{
	if(m_isDeleted) return;
	m_isDeleted = true;
	Deselect();

	// If this HD building has a LOD, delete it too
	if(m_lod && !m_lod->m_isDeleted)
		m_lod->Delete();

	// If this IS a LOD, delete all HD buildings that reference it
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_lod == this && !other->m_isDeleted)
			other->Delete();
	}
}

void
ObjectInst::Undelete(void)
{
	if(!m_isDeleted) return;
	m_isDeleted = false;

	// Also undelete the LOD pair
	if(m_lod && m_lod->m_isDeleted)
		m_lod->Undelete();

	// If this IS a LOD, undelete HD children
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_lod == this && other->m_isDeleted)
			other->Undelete();
	}
}

void
DeleteSelected(void)
{
	// Collect all instances that will be deleted (including LOD cascades)
	// First, gather the selected ones
	ObjectInst *toDelete[64];
	int numToDelete = 0;
	CPtrNode *p, *next;
	for(p = selection.first; p; p = next){
		next = p->next;
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && numToDelete < 64)
			toDelete[numToDelete++] = inst;
	}
	if(numToDelete == 0) return;

	// Now delete them and collect ALL that got deleted (including LOD cascade)
	ObjectInst *allDeleted[64];
	int numAllDeleted = 0;

	// Snapshot which are already deleted
	// Then delete, and find newly deleted ones
	for(int i = 0; i < numToDelete; i++)
		toDelete[i]->Delete();

	// Scan for all instances that are now deleted to record in undo
	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted && numAllDeleted < 64){
			// Check if this was freshly deleted (it's in our set or cascaded)
			// Simple approach: just record all deleted. On undo we undelete them.
			// This is slightly broad but safe.
		}
	}

	// Simpler: just record what we explicitly asked to delete,
	// the cascade is handled by Undelete() automatically
	UndoRecordDelete(toDelete, numToDelete);
}

// Clipboard
#define MAX_CLIPBOARD 64
static ObjectInst *clipboard[MAX_CLIPBOARD];
static int clipboardCount;

// Undo/Redo system
#define MAX_UNDO 128
static UndoAction undoStack[MAX_UNDO];
static int undoCount;	// number of actions in stack
static int undoPos;	// current position (next undo)

static void
updateRwFrameForInst(ObjectInst *inst)
{
	if(inst->m_rwObject == nil) return;
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	rw::Frame *f;
	if(obj->m_type == ObjectDef::ATOMIC)
		f = ((rw::Atomic*)inst->m_rwObject)->getFrame();
	else
		f = ((rw::Clump*)inst->m_rwObject)->getFrame();
	f->transform(&inst->m_matrix, rw::COMBINEREPLACE);
}

static void
pushUndo(UndoAction *a)
{
	// If we're not at the top, discard redo history
	undoCount = undoPos;
	if(undoCount >= MAX_UNDO){
		// Shift everything down
		memmove(&undoStack[0], &undoStack[1], (MAX_UNDO-1)*sizeof(UndoAction));
		undoCount--;
		undoPos--;
	}
	undoStack[undoCount] = *a;
	undoCount++;
	undoPos = undoCount;
}

void
UndoRecordMove(ObjectInst *inst, rw::V3d oldPos, ObjectInst *lodInst, rw::V3d lodOldPos)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_MOVE;
	a.inst = inst;
	a.oldPos = oldPos;
	a.newPos = inst->m_translation;
	a.lodInst = lodInst;
	a.lodOldPos = lodOldPos;
	if(lodInst)
		a.lodNewPos = lodInst->m_translation;
	pushUndo(&a);
}

void
UndoRecordRotate(ObjectInst *inst, rw::Quat oldRot)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_ROTATE;
	a.inst = inst;
	a.oldRot = oldRot;
	a.newRot = inst->m_rotation;
	pushUndo(&a);
}

void
UndoRecordDelete(ObjectInst **insts, int num)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_DELETE;
	a.numDeleted = num > 64 ? 64 : num;
	for(int i = 0; i < a.numDeleted; i++)
		a.deletedInsts[i] = insts[i];
	pushUndo(&a);
}

void
UndoRecordPaste(ObjectInst **insts, int num)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_PASTE;
	a.numPasted = num > 64 ? 64 : num;
	for(int i = 0; i < a.numPasted; i++)
		a.pastedInsts[i] = insts[i];
	pushUndo(&a);
}

void
Undo(void)
{
	if(undoPos <= 0) return;
	undoPos--;
	UndoAction *a = &undoStack[undoPos];

	switch(a->type){
	case UNDO_MOVE:
		a->inst->m_translation = a->oldPos;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		if(a->lodInst){
			a->lodInst->m_translation = a->lodOldPos;
			a->lodInst->UpdateMatrix();
			updateRwFrameForInst(a->lodInst);
		}
		break;
	case UNDO_ROTATE:
		a->inst->m_rotation = a->oldRot;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		break;
	case UNDO_DELETE:
		for(int i = 0; i < a->numDeleted; i++)
			a->deletedInsts[i]->Undelete();
		break;
	case UNDO_PASTE:
		for(int i = 0; i < a->numPasted; i++){
			a->pastedInsts[i]->Deselect();
			a->pastedInsts[i]->Delete();
		}
		break;
	}
}

void
Redo(void)
{
	if(undoPos >= undoCount) return;
	UndoAction *a = &undoStack[undoPos];
	undoPos++;

	switch(a->type){
	case UNDO_MOVE:
		a->inst->m_translation = a->newPos;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		if(a->lodInst){
			a->lodInst->m_translation = a->lodNewPos;
			a->lodInst->UpdateMatrix();
			updateRwFrameForInst(a->lodInst);
		}
		break;
	case UNDO_ROTATE:
		a->inst->m_rotation = a->newRot;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		break;
	case UNDO_DELETE:
		for(int i = 0; i < a->numDeleted; i++)
			a->deletedInsts[i]->Delete();
		break;
	case UNDO_PASTE:
		for(int i = 0; i < a->numPasted; i++){
			a->pastedInsts[i]->Undelete();
			a->pastedInsts[i]->Select();
		}
		break;
	}
}

ObjectInst*
GetInstanceByID(int32 id)
{
	CPtrNode *p;
	for(p = instances.first; p; p = p->next)
		if(((ObjectInst*)p->item)->m_id == id)
			return (ObjectInst*)p->item;
	return nil;
}

ObjectInst*
AddInstance(void)
{
	static int32 id = 1;

	ObjectInst *inst = new ObjectInst;
	memset(inst, 0, sizeof(ObjectInst));
	inst->m_id = id++;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	instances.InsertItem(inst);
	return inst;
}

void
CopySelected(void)
{
	clipboardCount = 0;
	CPtrNode *p;
	for(p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && clipboardCount < MAX_CLIPBOARD)
			clipboard[clipboardCount++] = inst;
	}
	if(clipboardCount > 0)
		log("Copied %d instance(s)\n", clipboardCount);
}

static ObjectInst*
cloneInstance(ObjectInst *src, GameFile *dstFile, int iplIndex, rw::V3d offset)
{
	ObjectInst *inst = AddInstance();
	inst->m_objectId = src->m_objectId;
	inst->m_area = src->m_area;
	inst->m_rotation = src->m_rotation;
	inst->m_translation.x = src->m_translation.x + offset.x;
	inst->m_translation.y = src->m_translation.y + offset.y;
	inst->m_translation.z = src->m_translation.z + offset.z;
	inst->m_isUnimportant = src->m_isUnimportant;
	inst->m_isUnderWater = src->m_isUnderWater;
	inst->m_isTunnel = src->m_isTunnel;
	inst->m_isTunnelTransition = src->m_isTunnelTransition;
	inst->m_isBigBuilding = src->m_isBigBuilding;
	inst->m_lodId = -1;
	inst->m_lod = nil;
	inst->m_numChildren = 0;
	inst->m_file = dstFile;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	inst->m_iplIndex = iplIndex;
	inst->m_isDirty = true;
	inst->UpdateMatrix();
	inst->CreateRwObject();
	InsertInstIntoSectors(inst);
	return inst;
}

void
PasteClipboard(void)
{
	if(clipboardCount == 0) return;

	rw::V3d offset = { 10.0f, 0.0f, 0.0f };

	ObjectInst *pasted[MAX_CLIPBOARD * 2];
	int numPasted = 0;

	// Deduplicate: skip LODs that are already the m_lod of another
	// clipboard entry (they'll be auto-copied with their HD parent)
	ObjectInst *toPaste[MAX_CLIPBOARD];
	int numToPaste = 0;

	for(int i = 0; i < clipboardCount; i++){
		bool isLodOfAnother = false;
		for(int j = 0; j < clipboardCount; j++){
			if(i != j && clipboard[j]->m_lod == clipboard[i]){
				isLodOfAnother = true;
				break;
			}
		}
		if(!isLodOfAnother)
			toPaste[numToPaste++] = clipboard[i];
	}

	ClearSelection();

	for(int i = 0; i < numToPaste; i++){
		ObjectInst *src = toPaste[i];
		ObjectInst *srcLod = src->m_lod;

		// Determine destination text IPL file.
		// For streaming instances, use the LOD's file (which is the text IPL).
		// If no LOD, scan for a text IPL instance from the same streaming base.
		GameFile *dstFile = nil;
		if(src->m_imageIndex < 0){
			// Source is text IPL — use same file
			dstFile = src->m_file;
		}else if(srcLod && srcLod->m_file){
			// Streaming HD — use its LOD's text IPL file
			dstFile = srcLod->m_file;
		}else{
			// Streaming HD with no LOD — find any text IPL instance
			// that shares this LOD's file as a fallback.
			// Last resort: just use src->m_file (will create standalone text IPL)
			dstFile = src->m_file;
		}

		// Find the max m_iplIndex for this file (text IPL instances only)
		int maxIplIndex = -1;
		CPtrNode *p;
		for(p = instances.first; p; p = p->next){
			ObjectInst *other = (ObjectInst*)p->item;
			if(other->m_file == dstFile && other->m_imageIndex < 0){
				if(other->m_iplIndex > maxIplIndex)
					maxIplIndex = other->m_iplIndex;
			}
		}

		// Paste LOD first (if any) so we have its iplIndex for the HD's lodId
		ObjectInst *newLod = nil;
		if(isSA() && srcLod && !srcLod->m_isDeleted){
			newLod = cloneInstance(srcLod, dstFile, ++maxIplIndex, offset);
			pasted[numPasted++] = newLod;
		}

		// Paste HD
		ObjectInst *newHd = cloneInstance(src, dstFile, ++maxIplIndex, offset);

		// Link LOD
		if(newLod){
			newHd->m_lodId = newLod->m_iplIndex;
			newHd->m_lod = newLod;
			newLod->m_numChildren = 1;
		}

		newHd->Select();
		pasted[numPasted++] = newHd;
	}

	if(numPasted > 0){
		UndoRecordPaste(pasted, numPasted);
		log("Pasted %d instance(s)\n", numPasted);
	}
}





int numSectorsX, numSectorsY;
CRect worldBounds;
Sector *sectors;
Sector outOfBoundsSector;

void
InitSectors(void)
{
	switch(params.map){
	case GAME_III:
		numSectorsX = 100;
		numSectorsY = 100;
		worldBounds.left = -2000.0f;
		worldBounds.bottom = -2000.0f;
		worldBounds.right = 2000.0f;
		worldBounds.top = 2000.0f;
		break;
	case GAME_VC:
		numSectorsX = 80;
		numSectorsY = 80;
		worldBounds.left = -2400.0f;
		worldBounds.bottom = -2000.0f;
		worldBounds.right = 1600.0f;
		worldBounds.top = 2000.0f;
		break;
	case GAME_SA:
		numSectorsX = 120;
		numSectorsY = 120;
		worldBounds.left = -3000.0f;
		worldBounds.bottom = -3000.0f;
		worldBounds.right = 3000.0f;
		worldBounds.top = 3000.0f;
		break;
	}
	sectors = new Sector[numSectorsX*numSectorsY];
}

Sector*
GetSector(int ix, int iy)
{
	return &sectors[ix*numSectorsY + iy];
}

//Sector*
//GetSector(float x, float y)
//{
//	int i, j;
//	assert(x > worldBounds.left && x < worldBounds.right);
//	assert(y > worldBounds.bottom && y < worldBounds.top);
//	i = (x + worldBounds.right - worldBounds.left)/numSectorsX;
//	j = (y + worldBounds.top - worldBounds.bottom)/numSectorsY;
//	return GetSector(i, j);
//}

int
GetSectorIndexX(float x)
{
	assert(x >= worldBounds.left && x < worldBounds.right);
	return (x + worldBounds.right - worldBounds.left)/numSectorsX;
}

int
GetSectorIndexY(float y)
{
	assert(y >= worldBounds.bottom && y < worldBounds.top);
	return (y + worldBounds.top - worldBounds.bottom)/numSectorsY;
}

bool
IsInstInBounds(ObjectInst *inst)
{
	ObjectDef *obj;
	// Some objects don't have collision data
	// TODO: figure out what to do with them
	obj = GetObjectDef(inst->m_objectId);
	if(obj->m_colModel == nil)
		return false;
	CRect bounds = inst->GetBoundRect();
	return bounds.left >= worldBounds.left &&
		bounds.right < worldBounds.right &&
		bounds.bottom >= worldBounds.bottom &&
		bounds.top < worldBounds.top;
}

static void
removeFromList(CPtrList *list, ObjectInst *inst)
{
	CPtrNode *p, *next;
	for(p = list->first; p; p = next){
		next = p->next;
		if(p->item == inst)
			list->DeleteNode(p);
	}
}

void
RemoveInstFromSectors(ObjectInst *inst)
{
	int x, y;

	// Remove from out of bounds sector
	removeFromList(&outOfBoundsSector.buildings, inst);
	removeFromList(&outOfBoundsSector.buildings_overlap, inst);
	removeFromList(&outOfBoundsSector.bigbuildings, inst);
	removeFromList(&outOfBoundsSector.bigbuildings_overlap, inst);

	// Remove from all sectors
	for(y = 0; y < numSectorsY; y++)
		for(x = 0; x < numSectorsX; x++){
			Sector *s = GetSector(x, y);
			removeFromList(&s->buildings, inst);
			removeFromList(&s->buildings_overlap, inst);
			removeFromList(&s->bigbuildings, inst);
			removeFromList(&s->bigbuildings_overlap, inst);
		}
}

void
InsertInstIntoSectors(ObjectInst *inst)
{
	Sector *s;
	CPtrList *list;
	int x, xstart, xmid, xend;
	int y, ystart, ymid, yend;

	if(!IsInstInBounds(inst)){
		list = inst->m_isBigBuilding ? &outOfBoundsSector.bigbuildings : &outOfBoundsSector.buildings;
		list->InsertItem(inst);
		return;
	}

	CRect bounds = inst->GetBoundRect();

	xstart = GetSectorIndexX(bounds.left);
	xend   = GetSectorIndexX(bounds.right);
	xmid   = GetSectorIndexX((bounds.left + bounds.right)/2.0f);
	ystart = GetSectorIndexY(bounds.bottom);
	yend   = GetSectorIndexY(bounds.top);
	ymid   = GetSectorIndexY((bounds.bottom + bounds.top)/2.0f);

	for(y = ystart; y <= yend; y++)
		for(x = xstart; x <= xend; x++){
			s = GetSector(x, y);
			if(x == xmid && y == ymid)
				list = inst->m_isBigBuilding ? &s->bigbuildings : &s->buildings;
			else
				list = inst->m_isBigBuilding ? &s->bigbuildings_overlap : &s->buildings_overlap;
			list->InsertItem(inst);
		}
}

