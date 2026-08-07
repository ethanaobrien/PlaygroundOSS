/* 
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "CKLBUtility.h"
#include "CKLBFormGroup.h"

extern void KLBUnregisterObjectName(void* object, const char* className);
extern void KLBRegisterObjectName(void* object, const char* className, int flags);

CKLBFormGroup::CKLBFormGroup() : m_begin(NULL) {
	KLBRegisterObjectName(this, "CKLBFormGroup", 0);
}
CKLBFormGroup::~CKLBFormGroup()
{
	KLBUnregisterObjectName(this, "CKLBFormGroup");
	release();
}

CKLBFormGroup&
CKLBFormGroup::getInstance()
{
	static CKLBFormGroup instance;
	return instance;
}

void
CKLBFormGroup::release()
{
	GROUP* pGroup = m_begin;
	while(pGroup) {
		GROUP* pNext = pGroup->next;
		checkGroup(pGroup);
		pGroup = pNext;
	}
	m_begin = NULL;
}

// 現在存在するグループと、その参照数・操作状態を一覧表示する。
void
CKLBFormGroup::dump()
{
	GROUP * pGrp = m_begin;
	while(pGrp) {
		printf("Group[%s][%i] %p : Working:%i (Worker:%p) (Locker:%p) \n",
			   pGrp->name, pGrp->refCount, pGrp,
			   pGrp->working, pGrp->worker, pGrp->locker);
		pGrp = pGrp->next;
	}
}

bool
CKLBFormGroup::addForm(SFormCtrlList * list, const char * group_name)
{
	// 指定されたコントロールリストがすでにどこかのグループに属していれば、一旦除外する
	delForm(list);

	// 新たに所属させるべきグループを得る。
	if(group_name) {
		GROUP * pGrp = createGroup(group_name);
		if(!pGrp) return false;
		list->pGroup = (void *)pGrp;
	}

	return true;
}

bool
CKLBFormGroup::delForm(SFormCtrlList * list)
{
	GROUP * pGrp = (GROUP *)list->pGroup;
	if(!pGrp) return true;

	if(pGrp->worker == list && pGrp->working) {
		pGrp->working = false;
	}
	list->pGroup = NULL;

	if(--pGrp->refCount == 0) {
		checkGroup(pGrp);
	}
	return true;
}

// 既存のグループから指定された名称のものを探す。
// なければ 0(NULL)を返す。
CKLBFormGroup::GROUP *
CKLBFormGroup::searchGroup(const char * group_name)
{
	GROUP * pGrp = m_begin;
	while(pGrp) {
		if(!CKLBUtility::safe_strcmp(pGrp->name, group_name)) {
			pGrp->refCount++;
			return pGrp;
		}
		pGrp = pGrp->next;
	}
	return NULL;
}

// 指定された名称のグループが無ければ生成する。
// 既に存在すれば既存のポインタを返す。
CKLBFormGroup::GROUP *
CKLBFormGroup::createGroup(const char * group_name)
{
	GROUP * pGrp = searchGroup(group_name);
	if(pGrp) {
		return pGrp;
	}

	pGrp = KLBNEW(GROUP);
	if(!pGrp) return NULL;

	pGrp->next = m_begin;
	pGrp->refCount = 1;
	m_begin = pGrp;

	pGrp->name = CKLBUtility::copyString(group_name);

	return pGrp;
}

void
CKLBFormGroup::checkGroup(GROUP * pGrp)
{
	KLBDELETEA(pGrp->name);
	GROUP * pPrev = NULL;
	GROUP * pCurrent = m_begin;
	while(pCurrent) {
		if(pCurrent == pGrp) {
			if(pPrev) {
				pPrev->next = pCurrent->next;
			} else {
				m_begin = pCurrent->next;
			}
			KLBDELETE(pCurrent);
			return;
		}
		pPrev = pCurrent;
		pCurrent = pCurrent->next;
	}
}

// グループの参照をひとつ手放す。最後の参照であればグループ自体を破棄する。
void
CKLBFormGroup::releaseGroup(GROUP * pGrp)
{
	if(!pGrp) return;

	if(--pGrp->refCount == 0) {
		checkGroup(pGrp);
	}
}
