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
#include "CKLBDebugMenu.h"
#include "CKLBDrawTask.h"
#include "CKLBTouchPad.h"
#include "CKLBUtility.h"
#include "CKLBLuaEnv.h"

extern void KLBUnregisterObjectName(void* object, const char* className);
extern void KLBRegisterObjectName(void* object, const char* className, int flags);

#define CAPTION_X	32
#define VALUE_X		300

#define EXIT_MARGINE 40
#define CAP_MARGINE 32
#define LINE_STEP 50
#define DEBUG_BACKGROUND_WIDTH 500
;
const char * CKLBDebugResource::ms_labelSwitch[] = { "OFF", "ON" };

CKLBDebugSign::CKLBDebugSign() : m_pMenu(NULL) {}
CKLBDebugSign::~CKLBDebugSign() {}

CKLBDebugSign *
CKLBDebugSign::create(CKLBDebugMenu * pMenu)
{
	if(!pMenu) return NULL;

	CKLBDebugSign * pTask = KLBNEW(CKLBDebugSign);
	if(!pTask) return NULL;

	if(!pTask->init(pMenu)) {
		KLBDELETE(pTask);
		return NULL;
	}
	return pTask;
}

bool
CKLBDebugSign::init(CKLBDebugMenu * pMenu)
{
	m_cmdStep = 0;
	m_cmdID = -1;
	m_pMenu = pMenu;

	// 画面サイズを取得
	CKLBDrawResource& res = CKLBDrawResource::getInstance();
	m_width = res.width();
	m_height = res.height();
	return regist(pMenu, P_UIPREV);
}

void
CKLBDebugSign::execute(u32 /* deltaT */)
{
	CKLBTouchPadQueue& tpq = CKLBTouchPadQueue::getInstance();
	tpq.startItem();

	const PAD_ITEM * item;
	bool bCommand = false;

	while(NULL != (item = tpq.getItem())) {
		switch(item->type)
		{
		case PAD_ITEM::TAP:
			{
				// 既にコマンド入力が別の指で始まっている。
				if(m_cmdStep > 0) break;
				if(checkPoint(item->x, item->y) != 1) {
					m_cmdStep = 0;
					break;
				}
				m_cmdStep = 1;
				m_cmdID = item->id;
			}
			break;
		case PAD_ITEM::DRAG:
			{
				// コマンド解釈とは別の指。
				if(m_cmdStep == 0 || m_cmdID != item->id) break;

				int check = checkPoint(item->x, item->y);
				if(check == m_cmdStep + 1) m_cmdStep = check;
			}
			break;
		case PAD_ITEM::RELEASE:
			{
				if(m_cmdStep != 4 || m_cmdID != item->id) {
					m_cmdStep = 0;
					break;
				}
				// ここに来る前に 4つのチェック条件を満たしている。
				// その上でチェック条件1の位置に戻ってきていたらコマンドが成立。
				int check = checkPoint(item->x, item->y);
				if(check == 1) {
					bCommand = true;
					m_cmdStep = 0;
					m_cmdID = -1;
				}
			}
			break;
		case PAD_ITEM::CANCEL:
			break;
		}
	}

	// コマンドが成立していたら、デバッグメニュータスクのモードを切り替える
	if(bCommand) m_pMenu->menu_switch(true);
}

void
CKLBDebugSign::die()
{
}

int
CKLBDebugSign::checkPoint(int x, int y)
{
	static int check[4] = { 1, 3, 4, 2 };
	int idx = (x <m_width / 2) ? 0 : 1;
	idx += (y < m_height / 2) ? 0 : 2;
	return check[idx];
}



CKLBDebugMenu::CKLBDebugMenu() : CKLBTask(), m_state(E_WAIT){}
CKLBDebugMenu::~CKLBDebugMenu()
{
	// メニューを閉じたままタスクが消えてもポーズが残らないようにする。
	CKLBTaskMgr::getInstance().setPause(false);
	m_state = E_WAIT;
	m_step  = 0;
}

CKLBDebugMenu *
CKLBDebugMenu::create()
{
	CKLBDebugMenu * pTask = KLBNEW(CKLBDebugMenu);
	if(!pTask) return NULL;

	pTask->regist(NULL, P_DBGMENU);
	CKLBTask * pChild = CKLBDebugSign::create(pTask);
	if(!pChild) {
		pTask->kill();
		KLBDELETE(pTask);
		return NULL;
	}
	return pTask;
}

bool
CKLBDebugMenu::init()
{
	regist(NULL, P_DBGMENU);
	CKLBTask * pChild = CKLBDebugSign::create(this);
	if(!pChild) {
		kill();
		return false;
	}
	return true;
}

void
CKLBDebugMenu::execute(u32 deltaT)
{
	if(m_state == E_ACTIVE) execute_active(deltaT);
}

void
CKLBDebugMenu::die()
{
	CKLBDebugResource& res = CKLBDebugResource::getInstance();
	res.release();
}

void
CKLBDebugMenu::menu_switch(bool bSwitch)
{
	CKLBTaskMgr::getInstance().setPause(bSwitch);
	STATE state = E_WAIT;
	if(bSwitch) {
		int screenHeight = CKLBDrawResource::getInstance().height();
		CKLBDebugResource& debug = CKLBDebugResource::getInstance();
		debug.setScreenHeight(screenHeight);
		state = E_ACTIVE;
	}
	m_state = state;
	m_step = 0;
}

void
CKLBDebugMenu::execute_active(u32 /*deltaT*/)
{
	enum {
		E_BEGIN = 0,
		E_LOOP,
		SCREEN_RIGHT_X = 801,
		SCREEN_TOP_Y = 199,
		SCREEN_BOTTOM_Y = 441
	};
	CKLBDebugResource& dbg = CKLBDebugResource::getInstance();

	switch(m_step)
	{
	case E_BEGIN:
		{
			dbg.dispEnable(true);
			m_dispY = dbg.setScroll(0);
			for(int i = 0; i < 5; i++) m_pad[i].enable = false;
			m_step = E_LOOP;
		}
		break;
	case E_LOOP:
		{
			CKLBTouchPadQueue& tpq = CKLBTouchPadQueue::getInstance();
			const PAD_ITEM * item;
			tpq.startItem();
			while(NULL != (item = tpq.getItem())) {
				switch(item->type)
				{
				case PAD_ITEM::TAP:
					{
						m_pad[item->id].enable = true;
						m_pad[item->id].x = item->x;
						m_pad[item->id].y = item->y;
					}
					break;
				case PAD_ITEM::DRAG:
					{
						if(!m_pad[item->id].enable) break;
						int mv_y = m_pad[item->id].y - item->y;
						int mv_x = m_pad[item->id].x - item->x;

						if(abs(mv_x) <= abs(mv_y)) {
							dbg.setScroll(m_dispY + mv_y);
						}
					}
					break;
				case PAD_ITEM::RELEASE:
					{
						if(item->x >= SCREEN_RIGHT_X && item->y <= SCREEN_TOP_Y) {
							dbg.dispEnable(false);
							dbg.finishReport(false);
							menu_switch(false);
							break;
						}
						if(item->x >= SCREEN_RIGHT_X && item->y >= SCREEN_BOTTOM_Y) {
							dbg.dispEnable(false);
							dbg.finishReport(true);
							dbg.dispEnable(true);
							m_dispY = dbg.setScroll(0);
							for(int i = 0; i < 5; i++) m_pad[i].enable = false;
							break;
						}

						if(!m_pad[item->id].enable) break;
						int mv_y = m_pad[item->id].y - item->y;

						m_dispY += mv_y;
						m_dispY = dbg.setScroll(m_dispY);

						bool bResult = dbg.tapItem(item->x, item->y);
						if(!bResult) {
							// [ EXIT ] が押されたので、デバッグメニューを終了する
							dbg.dispEnable(false);

							dbg.finishReport(false);

							menu_switch(false);
						}
					}
					break;
				case PAD_ITEM::CANCEL:
					break;
				}
			}
		}
		break;
	}
}



CKLBDebugResource::CKLBDebugResource() : m_begin(NULL), m_end(NULL), m_pNode(NULL), m_pBackground(NULL), m_order(0x7fffff00), m_callback(NULL), m_needsRebuild(false) {
	KLBRegisterObjectName(this, "CKLBDebugResource", 0);
	m_format = TexturePacker::getCurrentModeTexture();
}

CKLBDebugResource::~CKLBDebugResource()
{
	KLBUnregisterObjectName(this, "CKLBDebugResource");
	release();
}

CKLBDebugResource&
CKLBDebugResource::getInstance()
{
	static CKLBDebugResource instance;
	return instance;
}

void
CKLBDebugResource::release()
{
	GROUP * pItem = m_begin;
	while(pItem) {
		GROUP * pNext = pItem->next;

		SWITCH * sw = pItem->menu;
		for(int i = 0; i < pItem->cnt; i++) {
			KLBDELETEA(sw[i].key);
			if(sw[i].mode == DBG_MENU::M_SELECT) {
				for(int j = 0; j <= sw[i].max; j++) {
					KLBDELETEA(sw[i].disp[j]);
				}
				KLBDELETEA(sw[i].disp);
			}
		}
		KLBDELETEA(pItem->menu);
		KLBDELETEA(pItem->key);
		KLBDELETE(pItem);

		pItem = pNext;
	}
	m_begin = NULL;
	m_end = NULL;
	KLBDELETE(m_pNode);
	m_pNode = NULL;
	KLBDELETE(m_pBackground);
	m_pBackground = NULL;
	KLBDELETEA(m_callback);
	m_callback = NULL;
}


void
CKLBDebugResource::init()
{
	release();
	m_lastY = 0;
	m_dispY = 0;
	m_needsRebuild = true;
}

void
CKLBDebugResource::clear()
{
	m_lastY = 0;
	m_dispY = 0;
	m_needsRebuild = true;
}

void
CKLBDebugResource::create()
{
	if(!m_needsRebuild) return;
	m_needsRebuild = false;

	// 画面サイズを取得
	CKLBDrawResource& res = CKLBDrawResource::getInstance();
	m_scrnHeight = res.height();

	if(m_pNode) KLBDELETE(m_pNode);
	m_pNode = KLBNEW(CKLBNode);

	res.getRoot()->addNode(m_pNode);
	m_pNode->setTranslate(0, 0);
	m_pNode->setVisible(false);

	CKLBNode * pNode = KLBNEW(CKLBNode);
	m_pNode->addNode(pNode);
	m_lastY = EXIT_MARGINE;
	pNode->setTranslate(0, m_lastY);
	CKLBNodeVirtualDocument * pVDoc = createLabelItem("[ EXIT ]", &m_exitWidth, &m_exitHeight);
	pNode->addNode(pVDoc);
	pVDoc->setTranslate(0, 0);
	m_lastY += EXIT_MARGINE;

	KLBDELETE(m_pBackground);
	m_pBackground = KLBNEWC(CKLBNodeVirtualDocument, (true));
	m_pNode->addNode(m_pBackground);
	m_pBackground->m_useParentColor = false;
	m_pBackground->clear(0xff111111);
	m_pBackground->setViewPortSize(res.height(), DEBUG_BACKGROUND_WIDTH, 0.0f, 0.0f, m_order - 1, false);
	m_pBackground->setRotation(90.0f);
	m_pBackground->setViewPortPos(0, 0);
	m_pBackground->setTranslate(DEBUG_BACKGROUND_WIDTH, 0);
	m_pBackground->setVisible(false);
	res.getRoot()->addNode(m_pBackground);
}

CKLBNodeVirtualDocument *
CKLBDebugResource::createLabelItem(const char * label, int * width, int * height)
{
	create();
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	STextInfo txinfo;
	txinfo.parseInlineFormatting = false;
	void * pFont = platform.getFont(DBG_FONTSIZE, DBG_FONTNAME, 3);
	platform.getTextInfo(label, pFont, &txinfo, 1.0f, 1.0f);

	*width = txinfo.width;
	*height = txinfo.height;

	CKLBNodeVirtualDocument * pVDoc = KLBNEWC(CKLBNodeVirtualDocument, (true));
	pVDoc->createDocument(1,m_format);
	pVDoc->setDocumentSize(txinfo.width, txinfo.height);
	pVDoc->setViewPortSize(txinfo.width, txinfo.height, 0.0f, 0.0f, m_order, false);

	pVDoc->setFont(0, DBG_FONTNAME, DBG_FONTSIZE);	// 指定フォントをindex=0に指定
	pVDoc->clear(0);

	pVDoc->lockDocument();
	pVDoc->drawText(0, txinfo.top, (char *)label, 0xffffffff, 0);
	pVDoc->unlockDocument();

	pVDoc->setViewPortPos(0, 0);	// ViewPort とDocumentのサイズが同じで、かつ表示位置を(0,0)で固定
	pVDoc->markUpMatrix();
	pVDoc->setTranslate(0, 0);

	// Moved to improve font cache.
	platform.deleteFontResource(pFont);
	return pVDoc;
}

CKLBNode *
CKLBDebugResource::addLabel(int x, int y, const char * label, int * width, int * height)
{
	create();
	CKLBNodeVirtualDocument * pVDoc = createLabelItem(label, width, height);
	CKLBNode * pNode = KLBNEW(CKLBNode);
	pNode->addNode(pVDoc);
	m_pNode->addNode(pNode);

	pVDoc->setTranslate(0, 0);
	pNode->setTranslate(x, y);
	return pNode;
}

void
CKLBDebugResource::addDebugItem(const char * caption, const char * key, DBG_MENU * pItem)
{
	create();
	GROUP * pGrp = KLBNEW(GROUP);

	pGrp->prev = m_end;
	pGrp->next = NULL;
	if(pGrp->prev) {
		pGrp->prev->next = pGrp;
	} else {
		m_begin = pGrp;
	}
	m_end = pGrp;

	// グループキャプションの追加
	m_lastY += CAP_MARGINE;
	int width, height;
	pGrp->node = addLabel(0, m_lastY, caption, &width, &height);
	m_lastY += CAP_MARGINE;

	pGrp->key = CKLBUtility::copyString(key);

	// アイテム数のカウント
	pGrp->cnt = 0;
	for(int i = 0; pItem[i].caption; i++) pGrp->cnt++;

	// アイテム数と同じだけ、スイッチ配列を作る
	SWITCH * arrSwitch = KLBNEWA(SWITCH, pGrp->cnt);

	// アイテムリストに従い、スイッチ配列を初期化する
	for(int i = 0; pItem[i].caption; i++) {
		// アイテム項目の追加
		int width, height;
		CKLBNode * label = addLabel(CAPTION_X, m_lastY, pItem[i].caption, &width, &height);

		// モードを保存
		arrSwitch[i].label = label;
		arrSwitch[i].mode = pItem[i].mode;
		arrSwitch[i].val  = pItem[i].value;
		arrSwitch[i].pNode = KLBNEW(CKLBNode);

		m_pNode->addNode(arrSwitch[i].pNode);

		arrSwitch[i].key = CKLBUtility::copyString(pItem[i].key);
		arrSwitch[i].pNode->setTranslate(VALUE_X, m_lastY);
		arrSwitch[i].rect.x = VALUE_X;
		arrSwitch[i].rect.y = m_lastY;

		// モードに従い、スイッチ配列の値を初期化する
		switch(pItem[i].mode)
		{
		case DBG_MENU::M_SWITCH:	// ON/OFFのスイッチ
			{
				arrSwitch[i].min = 0;
				arrSwitch[i].max = 1;
				arrSwitch[i].disp = ms_labelSwitch;
				int idx = arrSwitch[i].val;
				const char * label = arrSwitch[i].disp[idx];
				int width, height;
				arrSwitch[i].obj.pVDoc = createLabelItem(label, &width, &height);
				arrSwitch[i].pNode->addNode(arrSwitch[i].obj.pVDoc);
				arrSwitch[i].rect.w = width;
				arrSwitch[i].rect.h = height;
			}
			break;
		case DBG_MENU::M_SELECT:	// 選択項目
			{
				arrSwitch[i].min = 0;
				arrSwitch[i].max = pItem[i].max;	// 項目数
				const char ** disp = KLBNEWA(const char *, pItem[i].max + 1);
				for(int n = 0; n <= arrSwitch[i].max; n++) {
					disp[n] = CKLBUtility::copyString(pItem[i].items[n]);
				}
				arrSwitch[i].disp = disp;
				int idx = arrSwitch[i].val;
				const char * label = arrSwitch[i].disp[idx];
				int width, height;
				arrSwitch[i].obj.pVDoc = createLabelItem(label, &width, &height);
				arrSwitch[i].pNode->addNode(arrSwitch[i].obj.pVDoc);
				arrSwitch[i].rect.w = width;
				arrSwitch[i].rect.h = height;
			}
			break;
		case DBG_MENU::M_NUMBER:	// 数値
			{
				arrSwitch[i].min = pItem[i].min;
				arrSwitch[i].max = pItem[i].max;
				arrSwitch[i].obj.pText = 0;
				arrSwitch[i].rect.w = DBG_FONTSIZE * 4;
				arrSwitch[i].rect.h = DBG_FONTSIZE + 8;
			}
			break;
		}
		m_lastY += LINE_STEP;
	}
	pGrp->menu = arrSwitch;
}

void
CKLBDebugResource::dispEnable(bool enable)
{
	create();
	m_pNode->setVisible(enable);
	m_pBackground->setVisible(enable);

	m_dispY = 0;	// 位置を 0 にリセットする
	m_pNode->setTranslate(0, m_dispY);

	GROUP * pGrp = m_begin;
	while(pGrp) {
		for(int i = 0; i < pGrp->cnt; i++) {
			SWITCH * sw = pGrp->menu + i;

			if(sw->mode != DBG_MENU::M_NUMBER) continue;
			if(enable) {
				CKLBUITextBox * pText = KLBNEWC(CKLBUITextBox, (false));
				sw->obj.pText = pText;
				sw->pNode->addNode(pText);
				pText->setWidth(sw->rect.w);
				pText->setHeight(sw->rect.h);
				char buf[11];
				sprintf(buf, "%d", sw->val);
				pText->setText(buf);
				pText->setTranslate(0, 0);
			} else {
				if(sw->obj.pText) {
					CKLBUITextBox * pText = sw->obj.pText;
					const char * str = pText->getText();
					// 数列ならば数値に変換し、新たな値にする
					bool result = true;
					for(const char * p = str; *p; p++) {
						if((*p < '0' || *p > '9') && (*p != '-')) {
							result = false;
							break;
						}
					}
					if(result) {
						int val = atoi(str);
						if(val < sw->min) val = sw->min;
						if(val > sw->max) val = sw->max;
						sw->val = val;
					}
					KLBDELETE(pText);
					sw->obj.pText = NULL;
				}
			}
		}
		pGrp = pGrp->next;
	}


}

bool
CKLBDebugResource::tapItem(int x, int y)
{
	int vx, vy;
	y -= m_dispY;
	vx = x;
	vy = y;

	// [ EXIT ] が押されたら、falseを返す
	if(vx >= 0 && vy >= EXIT_MARGINE && vx < m_exitWidth && vy < m_exitHeight + EXIT_MARGINE) {
		return false;
	}
	for(GROUP * pGrp = m_begin; pGrp; pGrp = pGrp->next) {
		for(int i = 0; i < pGrp->cnt; i++) {
			SWITCH * sw = pGrp->menu + i;
			vx = x - sw->rect.x;
			vy = y - sw->rect.y;
			if(vx >= 0 && vy >= 0 && vx < sw->rect.w && vy < sw->rect.h) {
				// 項目にヒット
				switch(sw->mode)
				{
				default:
					break;
				case DBG_MENU::M_SWITCH:
				case DBG_MENU::M_SELECT:
					{
						int val = sw->val + 1;
						if(val > sw->max) val = sw->min;
						sw->val = val;
						const char * label = sw->disp[val];
						int width, height;
						CKLBNodeVirtualDocument * pVDoc = createLabelItem(label, &width, &height);
						KLBDELETE(sw->obj.pVDoc);
						sw->pNode->addNode(pVDoc);
						sw->obj.pVDoc = pVDoc;
						sw->rect.w = width;
						sw->rect.h = height;
					}
					break;
				}
				return true;
			}
		}
	}
	return true;
}

void
CKLBDebugResource::createLuaTable(CLuaState& lua, bool screenRightDown)
{
	GROUP * pGrp = m_begin;

	lua.tableNew();
	if(screenRightDown) {
		lua.retString("screen_rightdown");
		lua.retBoolean(true);
		lua.tableSet();
	}
	while(pGrp) {
		lua.retString(pGrp->key);
		lua.tableNew();

		for(int i = 0; i < pGrp->cnt; i++) {
			SWITCH * sw = pGrp->menu + i;
			lua.retString(sw->key);
			switch(sw->mode)
			{
			case DBG_MENU::M_SWITCH:
				{
					bool bVal = (sw->val) ? true : false;
					lua.retBoolean(bVal);
				}
				break;
			case DBG_MENU::M_SELECT:
				{
					lua.retString(sw->disp[sw->val]);
				}
				break;
			case DBG_MENU::M_NUMBER:
				{
					lua.retInt(sw->val);
				}
				break;
			}
			lua.tableSet();
		}
		lua.tableSet();
		pGrp = pGrp->next;
	}
}

void
CKLBDebugResource::setGroupLua(const char * caption, const char * key, CLuaState& lua)
{
	// スタックに積んであるLuaテーブルの要素数を調べる
	int cnt = 0;
	lua.retNil();
	while(lua.tableNext()) {
		cnt++;
		lua.pop(1);
	}
	if(!cnt) return;

	DBG_MENU * arrMenu = KLBNEWA(DBG_MENU, cnt + 1);

	for(int i = 0; i < cnt; i++) {
		lua.retInt(i + 1);
		lua.tableGet();

		// 最上位に項目一つ分のLuaテーブルが乗っている
		lua.retInt(1); lua.tableGet(); arrMenu[i].mode = (DBG_MENU::MODE)lua.getInt(-1);	lua.pop(1);	// mode
		lua.retInt(2); lua.tableGet(); arrMenu[i].caption = lua.getString(-1);				lua.pop(1);	// caption
		lua.retInt(3); lua.tableGet(); arrMenu[i].key = lua.getString(-1);					lua.pop(1);	// key

		// ここから先はmodeで分岐
		switch(arrMenu[i].mode)
		{
		case DBG_MENU::M_SWITCH:
			{
				// デフォルト値のboolのみ
				lua.retInt(4); lua.tableGet(); arrMenu[i].value = (lua.getBool(-1)) ? 1 : 0; lua.pop(1);
				arrMenu[i].min = 0;
				arrMenu[i].max = 1;
				arrMenu[i].items = NULL;
			}
			break;
		case DBG_MENU::M_SELECT:
			{
				// デフォルト値
				arrMenu[i].value = 0;
				const char * value;
				lua.retInt(4); lua.tableGet(); value = lua.getString(-1); lua.pop(1);

				// 選択項目配列があるので、その数をカウントする
				lua.retInt(5); lua.tableGet();
				int itemcnt = 0;
				lua.retNil();
				while(lua.tableNext()) {
					itemcnt++;
					lua.pop(1);
				}
				const char ** disp = KLBNEWA(const char *, itemcnt);
				for(int idx = 0; idx < itemcnt; idx++) {
					lua.retInt(idx + 1);
					lua.tableGet();
					disp[idx] = lua.getString(-1);
					if(!CKLBUtility::safe_strcmp(value, disp[idx])) arrMenu[i].value = idx;
					lua.pop(1);
				}
				lua.pop(1);

				arrMenu[i].min = 0;
				arrMenu[i].max = itemcnt - 1;
				arrMenu[i].items = disp;
			}
			break;
		case DBG_MENU::M_NUMBER:
			{
				// 下限、上限、デフォルト値
				lua.retInt(4); lua.tableGet(); arrMenu[i].min = lua.getInt(-1); lua.pop(1);
				lua.retInt(5); lua.tableGet(); arrMenu[i].max = lua.getInt(-1); lua.pop(1);
				lua.retInt(6); lua.tableGet(); arrMenu[i].value = lua.getInt(-1); lua.pop(1);
				arrMenu[i].items = NULL;
			}
			break;
		}
		lua.pop(1);
	}

	// 末尾は caption をNULLにする
	arrMenu[cnt].caption = NULL;

	// 項目を登録する
	addDebugItem(caption, key, arrMenu);

	// もう不要なので、登録元に使用した領域を破棄する
	for(int i = 0; i < cnt; i++) {
		KLBDELETEA(arrMenu[i].items);
	}
	KLBDELETEA(arrMenu);
}

void
CKLBDebugResource::removeGroup(const char * key)
{
	GROUP * pGrp = m_begin;
	while(pGrp) {
		if(!CKLBUtility::safe_strcmp(pGrp->key, key)) break;
		pGrp = pGrp->next;
	}
	if(!pGrp) return;

	// リンクを切る
	if(pGrp->prev) {
		pGrp->prev->next = pGrp->next;
	} else {
		m_begin = pGrp->next;
	}
	if(pGrp->next) {
		pGrp->next->prev = pGrp->prev;
	} else {
		m_end = pGrp->prev;
	}

	for(int i = 0; i < pGrp->cnt; i++) {
		switch(pGrp->menu[i].mode) 
		{
		default:
			break;
		case DBG_MENU::M_SELECT:
			{
				for(int j = 0; j <= pGrp->menu[i].max; j++) {
					KLBDELETEA(pGrp->menu[i].disp[j]);
				}
				const char ** disp = pGrp->menu[i].disp;
				KLBDELETEA(disp);
			}
			break;
		}
		KLBDELETE(pGrp->menu[i].label);	// アイテムラベル削除
		KLBDELETE(pGrp->menu[i].pNode);	// コントロールノード削除
		KLBDELETEA(pGrp->menu[i].key);	// キー文字列削除
	}
	KLBDELETEA(pGrp->menu);
	KLBDELETE(pGrp->node);	// グループラベルノードの削除
	KLBDELETEA(pGrp->key);	// キー文字列削除
	KLBDELETE(pGrp);

	// 詰めた後の座標を再計算する
	pGrp = m_begin;
	m_lastY = EXIT_MARGINE;
	while(pGrp) {
		m_lastY += CAP_MARGINE;
		pGrp->node->setTranslate(0, m_lastY);
		m_lastY += CAP_MARGINE;
		for(int i = 0; i < pGrp->cnt; i++) {
			pGrp->menu[i].label->setTranslate(CAPTION_X, m_lastY);
			pGrp->menu[i].pNode->setTranslate(VALUE_X, m_lastY);
			pGrp->menu[i].rect.y = m_lastY;
			m_lastY += LINE_STEP;
		}
		pGrp = pGrp->next;
	}
}


void
CKLBDebugResource::setCallback(const char * callback)
{
	if(m_callback) {
		KLBDELETEA(m_callback);
		m_callback = NULL;
	}
	if(callback) {
		m_callback = CKLBUtility::copyString(callback);
	}
}

void
CKLBDebugResource::finishReport(bool screenRightDown)
{
	CLua_Only_State& lua = (CLua_Only_State&)CKLBLuaEnv::getInstance().getState();
	if(!m_callback) return;
	lua.getGlobal(m_callback);
	createLuaTable(lua, screenRightDown);
	lua.call(1, m_callback);
}
