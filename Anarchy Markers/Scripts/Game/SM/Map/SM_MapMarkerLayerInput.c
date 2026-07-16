// Runtime-шар мапи: життєвий цикл (OnMapOpen/OnMapClose/Update), ВЕСЬ поллінг вводу (миша/геймпад),
// слухачі дій, і HUD-оверлеї, що тікають щокадру (підказки керування, тултіпи, курсорні промпти,
// вказівники). Другий modded-блок SCR_MapMarkersUI — ланцюжок після SM_MapMarkerLayer.c.
//
// ЗАЛІЗНЕ ПРАВИЛО РОЗРІЗУ: modded-блоки компілюються за абеткою файлів, і цей файл іде ПІСЛЯ
// SM_MapMarkerLayer.c ("...Layer." < "...LayerI"), тож звідси видно все ядро — але ядро НЕ БАЧИТЬ
// нічого звідси. Тому:
//   - УСІ поля класу живуть у SM_MapMarkerLayer.c (тут лише m_bSMToolGuardBusy, бо його читає
//     один-єдиний метод, і той тут);
//   - метод, який кличе БУДЬ-ЩО з ядра — можна сюди; метод, який кличе ядро — НЕ МОЖНА
//     (SM_TryPanelBack/SM_TryTemplateBack/SM_PanelExit лишились у ядрі саме тому: їх кличе SM_NavBack).
// Двигун викликає OnMapOpen/Update/OnMapClose віртуально — йому байдуже, в якому блоці override.
modded class SCR_MapMarkersUI
{
	override void OnMapOpen(MapConfiguration config)
	{
		super.OnMapOpen(config);
		m_bSMMapOpen = true;
		m_bSMEditorMap = (config && config.MapEntityMode == EMapEntityMode.EDITOR);	// кеш: режим не змінюється за час відкриття
		m_iSMFeatures = AM_MapFeatures.ResolveForOpen(config);	// what we attach to this map screen

		// A host screen may pin the channel (and hide our picker) and may switch our layers off live.
		// Both are per-screen: reset them so nothing leaks in from the last map that was open.
		m_iSMForcedVis = AM_MapFeatures.ResolveForcedVisibilityForOpen(config);
		m_bSMPanelStartsHidden = AM_MapFeatures.ResolveDrawPanelHiddenForOpen(config);
		m_bSMDrawPanelShown = !m_bSMPanelStartsHidden;
		AM_MapFeatures.ResolveHintNudgeForOpen(config, m_fSMHintDX, m_fSMHintDY);
		AM_MapFeatures.ResetLayerVisible();
		m_bSMLastMarkersVisible = true;

		m_fSMPolicyRadius = 0;
		m_iSMPolicyMembershipMs = 0;
		m_fSMNextMembershipCheck = 0;
		AM_MapRenderPolicy policy = AM_MapFeatures.ResolvePolicyForOpen(config);
		if (policy)
		{
			m_fSMPolicyRadius = policy.m_fRadiusMeters;
			m_iSMPolicyMembershipMs = policy.m_iMembershipMs;
		}
		m_bSMNeedReposition = true;	// перший кадр — спозиціонувати все
		m_fSMLastZoom = -1;
		m_iSMLastRefX = -99999;
		m_iSMLastRefY = -99999;

		Widget mapRoot = m_MapEntity.GetMapMenuRoot();
		if (mapRoot)
		{
			m_wSMMapFrame = mapRoot.FindAnyWidget(SCR_MapConstants.MAP_FRAME_NAME);
		}
		// Мапа Game Master не має MapFrame (інший layout) — створюємо власний повноекранний оверлей,
		// бо наші мітки позиціонуються в екранних координатах.
		if (!m_wSMMapFrame)
			SM_CreateOwnMapFrame();
		// курсор (CursorImage) шукаємо ліниво в SM_SetMapCursorHidden — він на корені workspace, не під mapRoot

		// Полотно малювання + панель: на звичайній мапі гравця завжди (якщо дозволено конфігом);
		// у GM-мапі теж створюємо — рендер керується кнопкою «Player drawings», панель — «Drawing tools».
		// A view-only screen gets the canvas but no panel: drawings render, no tool can be armed.
		if (m_wSMMapFrame && SM_MarkerConfig.GetInstance().m_bAllowDrawing
			&& SM_HasFeature(AM_EMapFeature.DRAWINGS | AM_EMapFeature.DRAWING_TOOLS))
		{
			CanvasWidget cv = CanvasWidget.Cast(GetGame().GetWorkspace().CreateWidget(
				WidgetType.CanvasWidgetTypeID,
				WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
				Color.FromInt(0x00000000), 0, m_wSMMapFrame));	// z=0: штрихи під мітками й під панеллю
			if (cv)
			{
				FrameSlot.SetAnchorMin(cv, 0, 0);
				FrameSlot.SetAnchorMax(cv, 1, 1);
				m_DrawCanvas = new SM_DrawCanvas();
				m_DrawCanvas.Init(cv, m_MapEntity, m_wSMMapFrame, m_bSMEditorMap);
				m_DrawCanvas.SetRenderRadius(m_fSMPolicyRadius);
				if (m_iSMForcedVis >= 0)
					m_DrawCanvas.SetVisibility(m_iSMForcedVis);	// host pinned the channel
				if (SM_HasFeature(AM_EMapFeature.DRAWING_TOOLS))
				{
					m_DrawPanel = new SM_DrawPanel();
					m_DrawPanel.Build(m_DrawCanvas, m_wSMMapFrame, m_bSMEditorMap, m_iSMForcedVis, SM_HasFeature(AM_EMapFeature.TEMPLATES));
				}
			}
		}

		SM_GmState.s_OnMarkerViewChanged.Insert(SM_OnGmViewChanged);	// перебудова при перемиканні видимості зевса

		SM_RebuildAllVisuals();

		// AM_MapAction (натиск/утримання/відпуск) обробляємо ПОЛЛІНГОМ у Update — ванільний MapSelect
		// дає повторні/миттєві DOWN/UP і непридатний для детекту утримання.
		// Everything the player can rebind is our own action; only plain menu navigation still rides
		// on the vanilla Menu* actions, which is what they are for.
		// Listeners go in only for the features this screen actually has. Hiding a panel is not
		// enough: a registered listener keeps swallowing the key while the map is open.
		InputManager im = GetGame().GetInputManager();
		bool featMarkerTools = SM_HasFeature(AM_EMapFeature.MARKER_TOOLS);
		bool featDrawTools = (m_DrawPanel != null);
		if (featMarkerTools || featDrawTools)
		{
			im.AddActionListener("AM_Cancel", EActionTrigger.DOWN, SM_OnContext);
			im.AddActionListener("AM_Delete",   EActionTrigger.DOWN, SM_OnDelete);
			im.AddActionListener("AM_MarkerEdit", EActionTrigger.DOWN, SM_OnMarkerEdit);
			im.AddActionListener("AM_CopyLast",   EActionTrigger.DOWN, SM_OnCopyLast);
		}
		if (featDrawTools)
		{
			im.AddActionListener("AM_PanelFocus", EActionTrigger.DOWN, SM_OnPanelFocus);	// RB → панель малювання
			im.AddActionListener("AM_ToolPencil", EActionTrigger.DOWN, SM_OnToolPencil);
			im.AddActionListener("AM_ToolEraser", EActionTrigger.DOWN, SM_OnToolEraser);
			im.AddActionListener("AM_ToolFill",   EActionTrigger.DOWN, SM_OnToolFill);

			// Template buttons from the map side (pad A/B/Y/X by default, rebindable). Each press is a
			// remote click on the matching panel button and obeys the same visibility.
			im.AddActionListener("AM_TplApply",  EActionTrigger.DOWN, SM_OnTplApply);
			im.AddActionListener("AM_TplCancel", EActionTrigger.DOWN, SM_OnTplCancel);
			im.AddActionListener("AM_TplAdd",    EActionTrigger.DOWN, SM_OnTplAdd);
			im.AddActionListener("AM_TplRemove", EActionTrigger.DOWN, SM_OnTplRemove);

			// Пад-режим панелі: ванільні шорткати інструментів мапи (подвійний тап хрестовини = компас/
			// лінійка) не заглушити конфігом — тож відкочуємо: щойно інструмент увімкнувся в цей час,
			// одразу вимикаємо його назад.
			SCR_MapToolEntry.GetOnEntryToggledInvoker().Insert(SM_OnToolToggledGuard);
		}
		if (featMarkerTools)
		{
			// Консольна навігація діалогу (секційна модель): слухаємо Menu* DOWN, діємо лише коли активний контролер.
			im.AddActionListener("MenuUp",     EActionTrigger.DOWN, SM_NavUp);
			im.AddActionListener("MenuDown",   EActionTrigger.DOWN, SM_NavDown);
			im.AddActionListener("MenuLeft",   EActionTrigger.DOWN, SM_NavLeft);
			im.AddActionListener("MenuRight",  EActionTrigger.DOWN, SM_NavRight);
			im.AddActionListener("MenuSelect", EActionTrigger.DOWN, SM_NavSelect);
			im.AddActionListener("MenuBack",   EActionTrigger.DOWN, SM_NavBack);
			im.AddActionListener("MenuTabLeft",  EActionTrigger.DOWN, SM_NavTab);	// LB — перемикання вкладки іконок
			im.AddActionListener("MenuTabRight", EActionTrigger.DOWN, SM_NavTab);	// RB
			// Copy-last (Y / AM_CopyLastPad) опитуємо в SM_NavTick (надійніше за DOWN-слухача з Pressed).
		}

		if (featMarkerTools || featDrawTools)
			SM_BuildHint();		// no point advertising controls that aren't wired up
		if (SM_HasFeature(AM_EMapFeature.MARKERS))
			SM_BuildTooltip();	// тултіп при наведенні — у будь-якій мапі (зокрема GM)

		// Просимо актуальні мітки при кожному відкритті мапи (на хості SM_RequestSync сам no-op).
		// Гарантує мітки навіть якщо мапу відкрили з deploy-екрана ще до спавну (перший синк по
		// OnControlledEntityChanged тоді ще не спрацював). Ідемпотентно: сервер дошле лише дозволене, дублів нема по id.
		SCR_PlayerController syncPc = SM_LocalPC();
		if (syncPc)
		{
			syncPc.SM_RequestSync();
			syncPc.SM_RequestHostLocalSync();	// listen-host/SP: server sync is a no-op — activate own Locals from the persistence code
		}
	}

	override void OnMapClose(MapConfiguration config)
	{
		if (m_DrawPanel)
			m_DrawPanel.AbortTemplateFlow();	// a half-done template does not survive the map closing

		if (m_DrawCanvas)
			m_DrawCanvas.FinishLineChain();	// map closed mid-polyline; keep what was drawn

		SM_DrawOutbox.Flush();	// the map is closing and Tick stops — send whatever draw ops are still buffered

		SM_GmState.s_OnMarkerViewChanged.Remove(SM_OnGmViewChanged);
		if (m_bSMEditorUIHidden)
			SM_SetEditorUIHidden(false);	// мапа закрилась із відкритим діалогом — відписатись/повернути UI

		InputManager im = GetGame().GetInputManager();
		im.RemoveActionListener("AM_Cancel", EActionTrigger.DOWN, SM_OnContext);
		im.RemoveActionListener("AM_Delete",   EActionTrigger.DOWN, SM_OnDelete);
		im.RemoveActionListener("AM_MarkerEdit", EActionTrigger.DOWN, SM_OnMarkerEdit);
		im.RemoveActionListener("AM_CopyLast",   EActionTrigger.DOWN, SM_OnCopyLast);
		im.RemoveActionListener("AM_PanelFocus",     EActionTrigger.DOWN, SM_OnPanelFocus);
		im.RemoveActionListener("AM_ToolPencil", EActionTrigger.DOWN, SM_OnToolPencil);
		im.RemoveActionListener("AM_ToolEraser", EActionTrigger.DOWN, SM_OnToolEraser);
		im.RemoveActionListener("AM_ToolFill",   EActionTrigger.DOWN, SM_OnToolFill);
		im.RemoveActionListener("AM_TplApply",  EActionTrigger.DOWN, SM_OnTplApply);
		im.RemoveActionListener("AM_TplCancel", EActionTrigger.DOWN, SM_OnTplCancel);
		im.RemoveActionListener("AM_TplAdd",    EActionTrigger.DOWN, SM_OnTplAdd);
		im.RemoveActionListener("AM_TplRemove", EActionTrigger.DOWN, SM_OnTplRemove);
		SCR_MapToolEntry.GetOnEntryToggledInvoker().Remove(SM_OnToolToggledGuard);
		im.RemoveActionListener("MenuUp",     EActionTrigger.DOWN, SM_NavUp);
		im.RemoveActionListener("MenuDown",   EActionTrigger.DOWN, SM_NavDown);
		im.RemoveActionListener("MenuLeft",   EActionTrigger.DOWN, SM_NavLeft);
		im.RemoveActionListener("MenuRight",  EActionTrigger.DOWN, SM_NavRight);
		im.RemoveActionListener("MenuSelect", EActionTrigger.DOWN, SM_NavSelect);
		im.RemoveActionListener("MenuBack",   EActionTrigger.DOWN, SM_NavBack);
		im.RemoveActionListener("MenuTabLeft",  EActionTrigger.DOWN, SM_NavTab);
		im.RemoveActionListener("MenuTabRight", EActionTrigger.DOWN, SM_NavTab);
		m_bSMNavActive = false;
		m_bSMPlaceDown = false;
		SM_NavDestroyHL();

		if (m_wSMHintBox)
		{
			m_wSMHintBox.RemoveFromHierarchy();
			m_wSMHintBox = null;
		}
		m_aSMHintRows.Clear();
		m_iSMHintState = -1;
		if (m_wSMTooltip)
		{
			m_wSMTooltip.RemoveFromHierarchy();
			m_wSMTooltip = null;
		}
		if (m_wSMTooltipVis)
		{
			m_wSMTooltipVis.RemoveFromHierarchy();
			m_wSMTooltipVis = null;
		}
		m_iSMTipWX = -999999;
		m_iSMTipWY = -999999;
		m_iSMCarryId = -1;
		s_bSMCarrying = false;
		m_bSMLmbDown = false;
		m_bSMPickedThisPress = false;
		m_iSMPressMarkerId = -1;

		// якщо закрили мапу під час вказування — коректно зупинити й прибрати точки
		if (m_bSMPointing)
		{
			m_bSMPointing = false;
			if (m_CursorModule)
				m_CursorModule.HandleDialog(false);	// повернути інфо-текст, якщо вказували при закритті
			SM_SetMapCursorHidden(false);
			SCR_PlayerController lpc = SM_LocalPC();
			if (lpc)
				lpc.SM_RequestPointStop();
		}
		foreach (int pid, SM_PointerVisual pv : m_mSMPointerVis)
		{
			if (pv)
				pv.Destroy();
		}
		m_mSMPointerVis.Clear();
		SM_PointerHub.GetInstance().Clear();

		m_bSMMapOpen = false;
		SM_EndPreview();
		SM_ClearAllVisuals();
		if (m_wSMPlacePrompt)
		{
			m_wSMPlacePrompt.RemoveFromHierarchy();
			m_wSMPlacePrompt = null;
		}
		if (m_wSMTplPrompt)
		{
			m_wSMTplPrompt.RemoveFromHierarchy();
			m_wSMTplPrompt = null;
		}
		if (m_DrawPanel)
		{
			m_DrawPanel.Destroy();
			m_DrawPanel = null;
		}
		if (m_DrawCanvas)
		{
			m_DrawCanvas.Destroy();
			m_DrawCanvas = null;
		}
		m_bSMDrawDown = false;
		m_bSMHoldMoveDown = false;
		m_bSMHoldPointDown = false;
		if (m_bSMDrawCursorHidden)
		{
			// Курсор ховали під активний інструмент малювання. Віджет CursorImage живе на корені
			// workspace й переживає закриття мапи — тож ОБОВ'ЯЗКОВО вертаємо його видимість, інакше
			// при повторному відкритті ванільного курсора не буде (а наш кружечок ще не активний).
			SM_SetMapCursorHidden(false);
			m_bSMDrawCursorHidden = false;
		}
		if (m_bSMPadUiGuard)
		{
			m_bSMPadUiGuard = false;	// зняти «щит», якщо мапу закрили з активним інструментом/панеллю
			if (m_CursorModule)
				m_CursorModule.HandleDialog(false);
		}
		m_bSMPanelPadNav = false;

		if (m_bSMOwnFrame && m_wSMMapFrame)
			m_wSMMapFrame.RemoveFromHierarchy();	// наш оверлей GM-мапи — прибрати
		m_bSMOwnFrame = false;
		m_wSMMapFrame = null;
		m_wSMMapCursor = null;

		super.OnMapClose(config);
	}

	// Щокадрове позиціонування+масштаб. Тягнута мітка слідує за курсором (локально).
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);

		if (!m_bSMMapOpen)
			return;

		// Консольний контролер навігації діалогу: на рівні секцій «пришпилюємо» фокус до поточної
		// секції щокадру, щоб вбудована геометрична навігація рушія не блукала між секціями.
		SM_NavTick();

		// Масштаб «папір»: 1.0 на макс. наближенні, менше при віддаленні.
		float maxZoom = m_MapEntity.GetMaxZoom();
		float factor = 1.0;
		if (maxZoom > 0)
			factor = Math.Clamp(m_MapEntity.GetCurrentZoom() / maxZoom, SM_MIN_ZOOM_SCALE, 1.0);

		WorkspaceWidget ws = GetGame().GetWorkspace();
		SM_RefreshMapOffset(ws);	// map-widget -> frame shift; 0 on the fullscreen map, non-zero on a tablet

		SM_PollMouse();		// натиск/утримання/відпуск ЛКМ (до позиціонування цього кадру)
		s_bSMCarrying = (m_iSMCarryId != -1);	// для SM_DisableRadial: ПКМ під час перенесення не відкриває радіалку

		if (m_fSMPolicyRadius > 0)
			SM_TickPolicyMembership();
		SM_UpdateHint();
		SM_UpdateTooltip();	// «Edited by» при наведенні
		if (SM_IsEditorMap())
			SM_UpdatePlacePrompt();	// підказка «оберіть точку» біля курсора (режим Create Marker)
		SM_UpdateTemplatePrompt();	// підказка біля курсора для темплейтів (клікни/тримай/чекай)

		// Тягнута мітка слідує за курсором — оновлюємо ЩОКАДРУ (це одна мітка).
		if (m_iSMCarryId != -1)
		{
			int curWX, curWY;
			if (SM_GetCursorWorld(curWX, curWY))
			{
				SM_MarkerVisual cv = m_mSMVisuals.Get(m_iSMCarryId);
				if (cv)
					SM_PositionVisual(cv, curWX, curWY, factor, ws);
			}
		}

		// A host screen (a tablet with its own "show markers" toggle) can switch our markers off at
		// any time. Nothing is destroyed — we just stop showing them — so flipping it back is instant.
		bool markersVisible = AM_MapFeatures.MarkersVisible();
		if (markersVisible != m_bSMLastMarkersVisible)
		{
			m_bSMLastMarkersVisible = markersVisible;
			m_bSMNeedReposition = true;	// the loop below is what applies it
		}

		// Решту репозиціонуємо ЛИШЕ коли змінився вид (пан/зум) або набір міток — інакше пропускаємо.
		// (Розміри/масштаб не страждають: SM_PositionVisual рахує їх щоразу, а зум = «вид змінено».)
		if (SM_DetectViewChange() || m_bSMNeedReposition)
		{
			float fw, fh;
			SM_GetFrameSizeUnscaled(fw, fh, ws);

			foreach (int id, SM_MarkerVisual vis : m_mSMVisuals)
			{
				if (!vis || id == m_iSMCarryId || id == m_iSMHiddenMarkerId)
					continue;	// тягнута — окремо вище; прихована при редагуванні — не чіпаємо

				int sx, sy;
				m_MapEntity.WorldToScreen(vis.m_Data.m_iPosX, vis.m_Data.m_iPosY, sx, sy, true);
				float usx = ws.DPIUnscale(sx);
				float usy = ws.DPIUnscale(sy);
				float margin = AM_MarkerWidgets.BASE_SIZE * SM_SizeFactor(vis.m_Data.m_iSize) * factor;

				Widget mw = vis.GetMainWidget();
				if (!markersVisible || usx < -margin || usx > fw + margin || usy < -margin || usy > fh + margin)
				{
					// КУЛЛІНГ: поза екраном — ховаємо й не позиціонуємо
					if (mw) mw.SetVisible(false);
					if (vis.m_wLabel) vis.m_wLabel.SetVisible(false);
					if (vis.m_wTime) vis.m_wTime.SetVisible(false);
					continue;
				}

				if (mw) mw.SetVisible(true);
				if (vis.m_wLabel) vis.m_wLabel.SetVisible(true);
				if (vis.m_wTime) vis.m_wTime.SetVisible(vis.m_Data.m_iDate != 0);	// позначка часу — за датою
				SM_PositionVisual(vis, vis.m_Data.m_iPosX, vis.m_Data.m_iPosY, factor, ws);
			}
			m_bSMNeedReposition = false;
		}

		// Живе прев'ю в діалозі: оновлюємо вигляд із поточних виборів і показуємо в точці розміщення.
		// Сторож від «привидів»: якщо діалог закрився в обхід нашого CleanupMarkerEditWidget (напр. конфлікт
		// іншого мапного мода перехопив закриття) — самотужки прибираємо залишки наступним кадром:
		// знищуємо «зависле» живе прев'ю та повертаємо видимість схованої на час редагування реальної мітки.
		if (!m_MarkerEditRoot)
		{
			if (m_PreviewVisual)
				SM_EndPreview();
			if (m_iSMHiddenMarkerId != -1)
			{
				SM_SetMarkerVisible(m_iSMHiddenMarkerId, true);
				SM_MarkerVisual hv = m_mSMVisuals.Get(m_iSMHiddenMarkerId);
				if (hv && hv.m_Data)
					SM_PositionVisual(hv, hv.m_Data.m_iPosX, hv.m_Data.m_iPosY, factor, ws);
				m_iSMHiddenMarkerId = -1;
			}
		}

		if (m_MarkerEditRoot && m_PreviewVisual)
		{
			SM_UpdatePreviewData();
			SM_ApplyVisualData(m_PreviewVisual);
			SM_PositionVisual(m_PreviewVisual, m_iSMPlaceX, m_iSMPlaceY, factor, ws);
		}

		// Підсвітку вибраної кнопки видимості тримаємо щокадру — інакше hover-анімація WLib-кнопки
		// скине нашу прозорість і вибір «згубиться».
		if (m_MarkerEditRoot)
			SM_UpdateVisHighlight();

		// KB/M фокус-лок: поки поле назви в режимі вводу, не даємо ховеру сусідньої секції перехопити
		// клавіатуру — якщо write-mode злетів, одразу повертаємо його на поле. Лок знімають лише Enter/Esc.
		if (m_bSMTextLock && m_MarkerEditRoot && m_EditBoxComp && SM_NavOnKBM())
		{
			EditBoxWidget eb = EditBoxWidget.Cast(m_EditBoxComp.m_wEditBox);
			if (eb && !eb.IsInWriteMode())
				m_EditBoxComp.ActivateWriteMode(true);
		}

		SM_UpdatePointers(factor, ws);	// тимчасові вказівники (показати пальцем)

		// Малювання: перемальовка полотна за потреби; панель ховаємо під час діалогу редагування мітки.
		// GM-мапа: рендер штрихів — за кнопкою «Player drawings», панель — за кнопкою «Drawing tools».
		if (m_DrawCanvas)
		{
			// Same live switch as the markers, ANDed with the GM's own "Player drawings" button.
			bool drawVisible = AM_MapFeatures.DrawingsVisible();
			if (m_bSMEditorMap && !SM_GmState.s_bDrawView)
				drawVisible = false;
			m_DrawCanvas.SetRenderEnabled(drawVisible);
			m_DrawCanvas.Tick();
			// Поки інструмент активний — ванільний курсор мапи ховаємо (його заміняє наш кружечок).
			// Ховаємо ЩОКАДРУ: під час пану ваніль інакше вертає курсор з іконкою перетягування.
			bool drawCur = m_DrawCanvas.IsActive();
			if (drawCur)
				SM_SetMapCursorHidden(true);
			else if (m_bSMDrawCursorHidden)
				SM_SetMapCursorHidden(false);
			m_bSMDrawCursorHidden = drawCur;
		}
		if (m_DrawPanel)
		{
			bool baseAllowed = !m_MarkerEditRoot;	// ховаємо під час діалогу мітки / вимкнення зевсом
			if (m_bSMEditorMap)
				baseAllowed = baseAllowed && SM_GmState.s_bDrawPanel;
			baseAllowed = baseAllowed && m_bSMDrawPanelShown;	// host screen's own toolbar button
			m_DrawPanel.SetVisible(baseAllowed);
			m_DrawPanel.TickFocusHighlight();
			m_DrawPanel.TickTemplateState();	// the anchor is dropped on the MAP; the panel has to notice
			m_DrawPanel.TickNameField();		// hold the name field's write mode while typing

			// Анти-автофокус: рушій сам фокусить перший focusable-віджет при відкритті мапи.
			// Якщо фокус на панелі без свідомого входу — знімаємо, інакше пад залипає в меню.
			WorkspaceWidget fws = GetGame().GetWorkspace();
			bool focusInPanel = false;
			if (fws)
			{
				Widget pf = fws.GetFocusedWidget();
				if (pf && m_DrawPanel.ContainsWidget(pf))
				{
					if (m_DrawPanel.IsTypingName())
						focusInPanel = true;	// name field owns the keyboard — this anti-autofocus was what kept knocking it out
					else if (!m_bSMPanelPadNav)
						fws.SetFocusedWidget(null);
					else
						focusInPanel = true;
				}
				else if (m_bSMPanelPadNav)
				{
					// Фокус пішов із панелі (вибір значення зняв його) — виходимо з пад-режиму.
					SM_PanelExit();
				}
			}

			InputManager gim = GetGame().GetInputManager();
			bool onPad = gim && !gim.IsUsingMouseAndKeyboard();

			// Той самий контекст, що й діалог мітки: тримає мапу відкритою на B
			// (B тоді фаєрить MenuBack, його ловить SM_NavBack). Поновлюється щокадру.
			//
			// ONLY while the pad is inside our UI (panel or the name field). This context replaces the
			// map's own, which carries the stick panning — activate it out on the map and the camera
			// stops moving, exactly when placing a template needs it most. Out there our AM_* actions
			// are reachable anyway (the conf adds them to MapContext), so B still cancels a placement;
			// it just closes the map along with it, which is the lesser evil (see the note at the
			// bottom of this file for what was tried).
			//
			// NOT for the delete dialog: that one is a menu in its own right with its own input and
			// focus. Re-activating our context over it every frame is what left its buttons dead and
			// the pad stuck inside it with no way out.
			if (onPad && !m_DrawPanel.IsDeleteDialogOpen()
				&& (m_bSMPanelPadNav || m_DrawPanel.IsTypingName()))
				gim.ActivateContext("MapMarkerEditContext");

			if (focusInPanel && onPad)
				m_DrawPanel.HandleNavInput(gim);

			// Щит від ванільних пад-дій мапи (d-pad = меню інструментів тощо), лише поки
			// гравець падом у панелі. Активний інструмент мапу не глушить — стік має панорамувати.
			bool wantGuard = onPad && !m_MarkerEditRoot && !m_bSMPointing && focusInPanel;
			if (wantGuard != m_bSMPadUiGuard)
			{
				m_bSMPadUiGuard = wantGuard;
				if (m_CursorModule)
					m_CursorModule.HandleDialog(wantGuard);
			}
			// Панель закрили (зевс вимкнув кнопку) — інструмент теж вимикаємо, щоб ЛКМ повернувся редактору.
			if (m_bSMEditorMap && !SM_GmState.s_bDrawPanel && m_DrawCanvas && m_DrawCanvas.IsActive())
				m_DrawCanvas.SetActive(false);

			// Вигляд панелі + підказка R1 залежно від того, де зараз гравець (лише геймпад).
			if (!baseAllowed)
			{
				m_DrawPanel.SetHintMode(0);
			}
			// A template modal (naming, delete confirm) forces the panel fully visible: the name field
			// IS part of the panel, and on the pad this state is neither pad-nav nor an armed tool, so
			// without this it fell through to the idle branch below and went to opacity 0 — the text was
			// being typed into a panel nobody could see.
			else if (m_DrawPanel.IsModalBusy())
			{
				m_DrawPanel.SetPanelOpacity(1.0);
				m_DrawPanel.SetHintMode(0);
			}
			else if (!onPad)
			{
				m_DrawPanel.SetPanelOpacity(1.0);
				m_DrawPanel.SetHintMode(0);
			}
			else if (m_bSMPanelPadNav)
			{
				m_DrawPanel.SetPanelOpacity(1.0);
				m_DrawPanel.SetHintMode(3);
			}
			else if (m_DrawCanvas && m_DrawCanvas.IsActive())
			{
				m_DrawPanel.SetPanelOpacity(0.35);
				m_DrawPanel.SetHintMode(2);
			}
			else
			{
				m_DrawPanel.SetPanelOpacity(0.0);
				m_DrawPanel.SetHintMode(1);
			}
		}
	}

	// Рендер тимчасових вказівників із хабу: створюємо/оновлюємо точку+ім'я, прибираємо застарілі.
	protected void SM_UpdatePointers(float factor, WorkspaceWidget ws)
	{
		float now = System.GetTickCount() / 1000.0;
		SM_PointerHub hub = SM_PointerHub.GetInstance();
		hub.PruneStale(now, SM_POINT_TIMEOUT);
		map<int, ref SM_PointerData> pts = hub.GetAll();

		// прибрати візуали тих, кого вже немає
		array<int> gone = {};
		foreach (int id, SM_PointerVisual pv : m_mSMPointerVis)
		{
			if (!pts.Contains(id))
				gone.Insert(id);
		}
		foreach (int id : gone)
		{
			if (m_mSMPointerVis[id])
				m_mSMPointerVis[id].Destroy();
			m_mSMPointerVis.Remove(id);
		}

		// створити/оновити та спозиціонувати
		foreach (int id, SM_PointerData p : pts)
		{
			if (!p)
				continue;
			SM_PointerVisual pv = m_mSMPointerVis.Get(id);
			if (!pv)
			{
				pv = SM_BuildPointerVisual(id);
				if (!pv)
					continue;
				m_mSMPointerVis.Set(id, pv);
			}
			SM_PositionPointer(pv, p.m_iPosX, p.m_iPosY, factor, ws);
		}
	}

	protected SM_PointerVisual SM_BuildPointerVisual(int ownerId)
	{
		if (!m_wSMMapFrame)
			return null;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		// BLEND обов'язковий — вмикає альфа-змішування (без нього текстура малюється непрозорим квадратом).
		ImageWidget dot = ImageWidget.Cast(ws.CreateWidget(
			WidgetType.ImageWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(SM_FINGER_COLOR), 0, m_wSMMapFrame));
		if (!dot)
			return null;
		dot.LoadImageTexture(0, SM_FINGER_TEX);
		dot.SetColor(Color.FromInt(SM_FINGER_COLOR));	// тонуємо білу текстуру (форма/розмиття — в альфі)
		FrameSlot.SetAnchorMin(dot, 0, 0);
		FrameSlot.SetAnchorMax(dot, 0, 0);
		FrameSlot.SetAlignment(dot, 0.5, 0.5);	// пивот центр → SetPos позиціонує центр
		FrameSlot.SetSizeToContent(dot, false);

		TextWidget name = SM_BuildLabel();	// підпис під точкою (той самий стиль, що й мітки)
		if (name)
		{
			string n = "";
			PlayerManager pm = GetGame().GetPlayerManager();
			if (pm)
				n = pm.GetPlayerName(ownerId);
			name.SetText(n);
			name.SetColor(Color.FromInt(SM_FINGER_COLOR));
		}

		SM_PointerVisual pv = new SM_PointerVisual();
		pv.m_wDot = dot;
		pv.m_wName = name;
		return pv;
	}

	protected void SM_PositionPointer(SM_PointerVisual pv, int wx, int wy, float factor, WorkspaceWidget ws)
	{
		if (!pv || !pv.m_wDot)
			return;
		int sx, sy;
		m_MapEntity.WorldToScreen(wx, wy, sx, sy, true);
		float usx = ws.DPIUnscale(sx) + m_fSMMapOffX;	// map-widget -> our frame (0 on the fullscreen map)
		float usy = ws.DPIUnscale(sy) + m_fSMMapOffY;

		float size = AM_MarkerWidgets.BASE_SIZE * SM_SizeFactor(SM_POINT_SIZE) * factor;
		FrameSlot.SetSize(pv.m_wDot, size, size);
		FrameSlot.SetPos(pv.m_wDot, usx, usy);	// пивот центр → точка центрована на курсорі

		if (pv.m_wName)
		{
			float font = size * AM_MarkerWidgets.TEXT_RATIO;
			if (font < 2.0)
				pv.m_wName.SetVisible(false);
			else
			{
				pv.m_wName.SetVisible(true);
				pv.m_wName.SetExactFontSize(Math.Round(font));
				FrameSlot.SetPos(pv.m_wName, usx, usy + size * AM_MarkerWidgets.LABEL_OFFSET);
			}
		}
	}

	// Підказка «оберіть точку» біля курсора, поки активний режим Create Marker зевса.
	protected void SM_UpdatePlacePrompt()
	{
		bool show = SM_GmState.s_bCreatePending && !m_MarkerEditRoot && m_wSMMapFrame != null;
		if (show)
		{
			if (!m_wSMPlacePrompt)
			{
				WorkspaceWidget ws = GetGame().GetWorkspace();
				TextWidget t = TextWidget.Cast(ws.CreateWidget(
					WidgetType.TextWidgetTypeID,
					WidgetFlags.VISIBLE | WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
					Color.FromInt(0xFFF0A020), 0, m_wSMMapFrame));	// помаранчевий
				if (t)
				{
					t.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
					t.SetExactFontSize(20);
					FrameSlot.SetAnchorMin(t, 0, 0);
					FrameSlot.SetAnchorMax(t, 0, 0);
					FrameSlot.SetSizeToContent(t, true);
					t.SetText("Click a point to place the marker");
					m_wSMPlacePrompt = t;
				}
			}
			if (m_wSMPlacePrompt)
				FrameSlot.SetPos(m_wSMPlacePrompt, SCR_MapCursorInfo.x + 22, SCR_MapCursorInfo.y + 52);	// нижче, щоб не лізло на висоту/координати
		}
		else if (m_wSMPlacePrompt)
		{
			m_wSMPlacePrompt.RemoveFromHierarchy();
			m_wSMPlacePrompt = null;
		}
	}

	// Підказка біля курсора для темплейтів. Дзеркалить prompt зевса: помаранчевий текст під курсором.
	// The panel already carries buttons; this is for the eyes on the MAP, where the action happens —
	// what to click, and above all that a confirmed template needs the draw button HELD, which nothing
	// on screen said before.
	protected void SM_UpdateTemplatePrompt()
	{
		string txt;
		if (m_DrawPanel && m_DrawCanvas && !m_MarkerEditRoot && m_wSMMapFrame)
		{
			SM_TemplateSession sess = SM_TemplateSession.GetInstance();
			bool armed = m_DrawCanvas.IsActive() && m_DrawCanvas.GetTool() == SM_DrawCanvas.TOOL_TEMPLATE;

			int shapeMode = m_DrawCanvas.GetShapeMode();
			if (armed && shapeMode != SM_ShapeGeometry.SHAPE_NONE)
			{
				bool second = m_DrawCanvas.ShapeFirstSet();
				if (shapeMode == SM_ShapeGeometry.SHAPE_GRID)
				{
					if (second)
						txt = "Click to finish the grid (min 10 x 10 squares)";
					else
						txt = "Click a grid square — it becomes A-1";
				}
				else if (shapeMode == SM_ShapeGeometry.SHAPE_CIRCLE)
				{
					if (second)
						txt = "Click to set the radius";
					else
						txt = "Click the centre of the circle";
				}
				else
				{
					if (second)
						txt = "Click the opposite corner";
					else
						txt = "Click the first corner";
				}
			}
			else if (sess.IsConfirmed())
			{
				if (sess.IsRateWaiting())
					txt = "Drawing limit reached — keep holding, it will continue";
				else if (!m_DrawCanvas.IsTemplateHeld())
					txt = "Hold to draw the template";
				// held and drawing: no prompt, the strokes speak for themselves
			}
			else if (sess.IsAnchored())
				txt = "Press Apply to draw it, or Cancel to move it";
			else if (armed && sess.IsPlaced())
			{
				// placed but not confirmed can't happen without anchored above; kept for clarity
			}
			else if (armed && SM_TemplateStore.GetInstance().Selected())
				txt = "Click the map to place the template";
		}

		if (txt == "")
		{
			if (m_wSMTplPrompt)
			{
				m_wSMTplPrompt.RemoveFromHierarchy();
				m_wSMTplPrompt = null;
			}
			return;
		}

		if (!m_wSMTplPrompt)
		{
			WorkspaceWidget ws = GetGame().GetWorkspace();
			TextWidget t = TextWidget.Cast(ws.CreateWidget(
				WidgetType.TextWidgetTypeID,
				WidgetFlags.VISIBLE | WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
				Color.FromInt(0xFFF0A020), 0, m_wSMMapFrame));	// той самий помаранчевий, що й у зевса
			if (!t)
				return;
			t.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
			t.SetExactFontSize(20);
			FrameSlot.SetAnchorMin(t, 0, 0);
			FrameSlot.SetAnchorMax(t, 0, 0);
			FrameSlot.SetSizeToContent(t, true);
			m_wSMTplPrompt = t;
		}
		m_wSMTplPrompt.SetText(txt);
		FrameSlot.SetPos(m_wSMTplPrompt, SCR_MapCursorInfo.x + 22, SCR_MapCursorInfo.y + 52);
	}

	// Підказка керування внизу-зліва. Glyph-рядки у ванільному стилі: гліф клавіші/кнопки + підпис
	// через SCR_InputButtonComponent.
	//
	// The map has no control-hint bar of its own to join — dumping the whole MapMenu tree in-game
	// turned up 577 widgets and not one vanilla hint button. So this box IS the bar, and its position
	// is ours to pick.
	protected void SM_BuildHint()
	{
		if (!m_wSMMapFrame)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		// Контейнер: вертикальний стек рядків, прикріплений до нижнього-лівого кута карти
		Widget box = ws.CreateWidget(
			WidgetType.VerticalLayoutWidgetTypeID,
			WidgetFlags.VISIBLE | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0x00000000), 0, m_wSMMapFrame);
		if (!box)
			return;
		FrameSlot.SetAnchorMin(box, 0, 1);
		FrameSlot.SetAnchorMax(box, 0, 1);
		FrameSlot.SetAlignment(box, 0, 1);	// нижній-лівий півот блока на якорі
		FrameSlot.SetSizeToContent(box, true);
		// У GM-мапі підказки вище (над підказками редактора) і трохи правіше, щоб не накладались.
		float hintX = SM_HINT_X;
		float hintY = SM_HINT_Y;
		if (SM_IsEditorMap())
		{
			hintX = 200;
			hintY = -340;
		}
		hintX += m_fSMHintDX;	// host screen (a tablet) clearing its own bottom-left chrome
		hintY += m_fSMHintDY;
		FrameSlot.SetPos(box, hintX, hintY);
		m_wSMHintBox = box;

		// 5 рядків-підказок (макс. для будь-якого стану). Переюзаємо ванільний WLib_NavigationButtonSmall.
		m_aSMHintRows.Clear();
		for (int i = 0; i < 5; i++)
		{
			Widget row = ws.CreateWidgets("{CB8563509DEF3E0E}UI/layouts/WidgetLibrary/Buttons/WLib_NavigationButtonSmall.layout", box);
			if (!row)
				continue;
			row.SetFlags(WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS);	// пасивна підказка — без наведення/фокусу
			LayoutSlot.SetHorizontalAlign(row, LayoutHorizontalAlign.Left);	// по лівому краю, як ванільний текст знизу
			m_aSMHintRows.Insert(row);
		}

		m_iSMHintState = -1;	// форсуємо перше наповнення
		SM_UpdateHint();
	}

	// Тултіп при наведенні на мітку («Edited by» + рядок видимості). Окремо від хінт-боксу, бо в
	// GM-мапі ми хінт-бокс не будуємо, а тултіп лишаємо.
	protected void SM_BuildTooltip()
	{
		if (!m_wSMMapFrame)
			return;
		WorkspaceWidget ws = GetGame().GetWorkspace();

		// Тултіп «Edited by» (біля курсора; з'являється при наведенні на мітку)
		TextWidget tip = TextWidget.Cast(ws.CreateWidget(
			WidgetType.TextWidgetTypeID,
			WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0xFF000000), 0, m_wSMMapFrame));	// чорний
		if (tip)
		{
			tip.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
			tip.SetExactFontSize(16);
			FrameSlot.SetAnchorMin(tip, 0, 0);
			FrameSlot.SetAnchorMax(tip, 0, 0);
			FrameSlot.SetSizeToContent(tip, true);
			tip.SetVisible(false);
			m_wSMTooltip = tip;
		}

		// Рядок видимості мітки (під «Edited by»), колір за областю: Local-сірий/Group-зелений/Side-синій/Global-червоний
		TextWidget tipv = TextWidget.Cast(ws.CreateWidget(
			WidgetType.TextWidgetTypeID,
			WidgetFlags.NOWRAP | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS,
			Color.FromInt(0xFFFFFFFF), 0, m_wSMMapFrame));
		if (tipv)
		{
			tipv.SetFont("{3E7733BAC8C831F6}UI/Fonts/RobotoCondensed/RobotoCondensed_Regular.fnt");
			tipv.SetExactFontSize(16);
			FrameSlot.SetAnchorMin(tipv, 0, 0);
			FrameSlot.SetAnchorMax(tipv, 0, 0);
			FrameSlot.SetSizeToContent(tipv, true);
			tipv.SetVisible(false);
			m_wSMTooltipVis = tipv;
		}
	}

	// Наповнення рядків підказки за поточним станом (тягнемо мітку чи ні). SetAction лише при зміні
	// стану — не щокадру (інакше зайвий перерахунок гліфів).
	protected void SM_UpdateHint()
	{
		if (m_aSMHintRows.IsEmpty())
			return;

		if (SM_IsEditorMap())
		{
			SM_UpdateHintEditor();
			return;
		}

		bool allowPtr  = SM_MarkerConfig.GetInstance().m_bAllowPointer;
		bool allowCopy = SM_MarkerConfig.GetInstance().m_bAllowCopyLast;

		InputManager him = GetGame().GetInputManager();
		bool pad = him && !him.IsUsingMouseAndKeyboard();	// геймпад → показуємо наші AM-кнопки (A/Y/X/B)

		// The template flow owns the map while any of its states is on — before this block the tool
		// fell into "normal" and the bar advertised marker controls that would not work.
		SM_TemplateSession tsess = SM_TemplateSession.GetInstance();

		int baseState;
		if (m_MarkerEditRoot)
			baseState = 2;	// відкритий діалог редагування/створення
		else if (m_bSMPanelPadNav)
			baseState = 5;	// пад: фокус у панелі малювання (навігація меню)
		else if (m_bSMPointing)
			baseState = 6;
		else if (m_iSMCarryId != -1)
			baseState = 1;	// перенесення мітки
		else if (m_DrawPanel && m_DrawPanel.IsTypingName())
			baseState = 8;	// темплейти: модал вводу назви
		else if (tsess.IsAnchored())
			baseState = 9;	// темплейти: привид на якорі, чекає Apply
		else if (tsess.IsConfirmed())
			baseState = 10;	// темплейти: підтверджено, малюється утриманням
		else if (m_DrawCanvas && m_DrawCanvas.IsActive() && m_DrawCanvas.GetTool() == SM_DrawCanvas.TOOL_SELECT)
			baseState = 11;	// темплейти: рамка виділення для збереження
		else if (m_DrawCanvas && m_DrawCanvas.IsActive() && m_DrawCanvas.GetTool() == SM_DrawCanvas.TOOL_TEMPLATE)
			baseState = 12;	// темплейти: привид у руці, шукає місце
		else if (m_DrawPanel && m_DrawPanel.IsTemplatesOpen())
			baseState = 13;	// темплейти: відкритий список (перегляд)
		else if (m_DrawCanvas && m_DrawCanvas.IsActive() && m_DrawCanvas.GetTool() == 0)
			baseState = 3;	// малювання: олівець
		else if (m_DrawCanvas && m_DrawCanvas.IsActive() && m_DrawCanvas.GetTool() == 1)
			baseState = 4;	// малювання: гумка
		else if (m_DrawCanvas && m_DrawCanvas.IsActive() && m_DrawCanvas.GetTool() == 2)
			baseState = 7;	// малювання: заливка — без цієї гілки вона падала у «звичайний» і рекламувала мітки
		else
			baseState = 0;	// звичайний

		// Ключ стану з прапорцями конфігу + девайсом — щоб їх зміна перебудувала рядки.
		int state = baseState;
		if (!allowPtr)
			state += 100;
		if (!allowCopy)
			state += 200;
		if (pad)
			state += 500;
		if (m_DrawPanel && m_DrawPanel.IsTemplatesOpen() && m_DrawPanel.HasTemplatePicked())
			state += 1000;	// перегляд списку: підсвічений слот вмикає рядки Place/Remove

		if (state == m_iSMHintState)
			return;
		m_iSMHintState = state;

		if (baseState == 2)	// діалог: на паді X видаляє пресет, на KB/M — ПКМ
		{
			if (pad)
				SM_SetHintRow(0, "AM_Delete", "Delete preset");
			else
				SM_SetHintRow(0, "AM_Cancel", "Delete preset");
			SM_SetHintRowVisible(1, false);
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 1)	// несемо мітку
		{
			if (pad)
			{
				SM_SetHintRow(0, "AM_MapAction",        "Release — place");	// відпустив A — поклав
				SM_SetHintRow(1, "AM_Cancel", "Cancel");			// B
			}
			else
			{
				SM_SetHintRow(0, "AM_MapAction",         "Place");
				SM_SetHintRow(1, "AM_Cancel", "Cancel");
			}
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 5)	// пад: навігація панеллю малювання — підказки показує лівий стовпчик біля панелі
		{
			SM_SetHintRowVisible(0, false);
			SM_SetHintRowVisible(1, false);
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 6)	// вказуємо пальцем (палець водиться стіком/паном, відпуск — стоп)
		{
			if (pad)
				SM_SetHintRow(0, "AM_MapAction", "Release — stop pointing");
			else
				SM_SetHintRow(0, "AM_MapAction", "Release — stop pointing");
			SM_SetHintRowVisible(1, false);
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		// Інструменти малювання. Гліф щоразу з реального бінда, а модифікатор має власний рядок —
		// писати «Shift + drag» словами в підпис означало б збрехати першому ж, хто його перебіндить.
		else if (baseState == 3)	// малювання: олівець
		{
			SM_SetHintRow(0, "AM_MapAction", "Draw");
			SM_SetHintRow(1, "AM_Cancel",    "Cancel tool");
			SM_SetHintRow(2, "AM_Delete",    "Remove stroke");
			if (pad)
				SM_SetHintRowVisible(3, false);	// на паді модифікатора прямої лінії немає
			else
				SM_SetHintRow(3, "AM_LineModifier", "+ draw — straight line");
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 4)	// малювання: гумка
		{
			SM_SetHintRow(0, "AM_MapAction", "Erase");
			SM_SetHintRow(1, "AM_Cancel",    "Cancel tool");
			SM_SetHintRow(2, "AM_Delete",    "Remove stroke");
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 7)	// малювання: заливка
		{
			SM_SetHintRow(0, "AM_MapAction", "Fill area");
			SM_SetHintRow(1, "AM_Cancel",    "Cancel tool");
			SM_SetHintRow(2, "AM_Delete",    "Remove fill");
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		// Template states. On KB/M the panel's buttons are right there for the mouse, so only the map
		// GESTURES get a row; the pad gets its A/B/Y/X, and those glyphs also sit beside the buttons.
		else if (baseState == 8)	// темплейти: ввід назви
		{
			if (pad)
			{
				SM_SetHintRow(0, "AM_TplAdd",    "Confirm name");
				SM_SetHintRow(1, "AM_TplCancel", "Back");
				SM_SetHintRowVisible(2, false);
			}
			else
			{
				// The keyboard is busy typing — advertising keys here would be advertising trouble.
				SM_SetHintRowVisible(0, false);
				SM_SetHintRowVisible(1, false);
				SM_SetHintRowVisible(2, false);
			}
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 9)	// темплейти: на якорі
		{
			if (pad)
			{
				SM_SetHintRow(0, "AM_TplApply",  "Apply — start drawing");
				SM_SetHintRow(1, "AM_TplCancel", "Move it again");
			}
			else
			{
				SM_SetHintRow(0, "AM_Cancel", "Move it again");
				SM_SetHintRowVisible(1, false);
			}
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 10)	// темплейти: підтверджено — малює утримання
		{
			SM_SetHintRow(0, "AM_MapAction", "Hold — draw template");
			SM_SetHintRowVisible(1, false);	// Discard навмисно лише кнопкою: він стирає вже намальоване
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 11)	// темплейти: рамка виділення
		{
			SM_SetHintRow(0, "AM_MapAction", "Drag a box around your drawings");
			if (pad)
			{
				SM_SetHintRow(1, "AM_TplAdd",    "Save template");
				SM_SetHintRow(2, "AM_TplCancel", "Cancel");
			}
			else
			{
				SM_SetHintRow(1, "AM_Cancel", "Cancel");
				SM_SetHintRowVisible(2, false);
			}
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 12)	// темплейти: привид у руці
		{
			SM_SetHintRow(0, "AM_MapAction", "Place template");
			if (pad)
				SM_SetHintRow(1, "AM_TplCancel", "Cancel");
			else
				SM_SetHintRow(1, "AM_Cancel", "Cancel");
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (baseState == 13)	// темплейти: перегляд списку
		{
			bool tplPicked = m_DrawPanel && m_DrawPanel.HasTemplatePicked();
			if (pad && tplPicked)
			{
				SM_SetHintRow(0, "AM_TplApply",  "Place template");
				SM_SetHintRow(1, "AM_TplRemove", "Delete template");
				SM_SetHintRow(2, "AM_TplAdd",    "New template");
			}
			else if (pad)
			{
				SM_SetHintRow(0, "AM_TplAdd", "New template");
				SM_SetHintRowVisible(1, false);
				SM_SetHintRowVisible(2, false);
			}
			else
			{
				// Mouse users click the buttons themselves; showing marker controls here was the bug.
				SM_SetHintRowVisible(0, false);
				SM_SetHintRowVisible(1, false);
				SM_SetHintRowVisible(2, false);
			}
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		// Screens without MARKER_TOOLS (a tablet) can't edit, move, delete or copy a marker — so none of
		// that may be advertised there. What's left is what the screen can actually do: point, and open
		// the drawing panel.
		else if (!SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
		{
			int row = 0;
			if (pad && m_DrawPanel)
			{
				SM_SetHintRow(row, "AM_PanelFocus", "Drawing tools");
				row++;
			}
			if (allowPtr)
			{
				if (pad)
					SM_SetHintRow(row, "AM_Pointer", "Point at map");
				else
					SM_SetHintRow(row, "AM_Pointer", "Point at map");
				row++;
			}
			for (int i = row; i < 5; i++)
				SM_SetHintRowVisible(i, false);
		}
		else if (pad)	// звичайний — геймпад (A: тап=редаг/створ, утримання=нести/вказувати)
		{
			SM_SetHintRow(0, "AM_MapAction", "Tap — edit / create");
			SM_SetHintRow(1, "AM_MarkerMove", "Move marker");
			SM_SetHintRow(2, "AM_Delete",  "Remove marker");
			if (allowPtr)
				SM_SetHintRow(3, "AM_Pointer", "Point at map");
			else
				SM_SetHintRowVisible(3, false);
			if (allowCopy)
				SM_SetHintRow(4, "AM_CopyLastPad", "Copy last marker");
			else
				SM_SetHintRowVisible(4, false);
		}
		else	// звичайний — KB/M
		{
			SM_SetHintRow(0, "AM_MarkerMove",     "Move marker");
			SM_SetHintRow(1, "AM_MarkerEdit",      "Edit / create marker");
			SM_SetHintRow(2, "AM_Delete", "Remove marker / stroke");
			if (allowPtr)
				SM_SetHintRow(3, "AM_Pointer",     "Point at map");
			else
				SM_SetHintRowVisible(3, false);	// вказування вимкнено — без підказки
			if (allowCopy)
				SM_SetHintRow(4, "AM_CopyLast",    "Copy last marker");
			else
				SM_SetHintRowVisible(4, false);	// копіювання вимкнено — без підказки
		}
	}

	// Підказки керування в мапі Game Master (видимість ON): рух/редагування/видалення/копія.
	protected void SM_UpdateHintEditor()
	{
		InputManager him = GetGame().GetInputManager();
		bool pad = him && !him.IsUsingMouseAndKeyboard();

		int est;
		if (m_MarkerEditRoot)
			est = 1000;	// відкритий діалог — без підказок
		else if (!SM_GmState.s_bMarkerView)
			est = 1001;	// видимість вимкнена — без підказок
		else if (m_iSMCarryId != -1)
			est = 1002;	// несемо мітку
		else
			est = 1003;	// звичайний GM-перегляд

		if (pad)
			est += 5000;	// девайс у ключ стану — щоб перемикання пад/KB/M перебудувало рядки

		if (est == m_iSMHintState)
			return;
		m_iSMHintState = est;

		if (est == 1002 || est == 6002)	// несемо мітку
		{
			if (pad)
			{
				SM_SetHintRow(0, "AM_MapAction",        "Release — place");
				SM_SetHintRow(1, "AM_Cancel", "Cancel");	// B
			}
			else
			{
				SM_SetHintRow(0, "AM_MapAction", "Place");
				SM_SetHintRowVisible(1, false);
			}
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (est == 6003)	// звичайний GM — геймпад
		{
			SM_SetHintRow(0, "AM_MapAction", "Tap — edit / create");
			SM_SetHintRow(1, "AM_MarkerMove", "Move marker");
			SM_SetHintRow(2, "AM_Delete",  "Remove");
			if (SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
				SM_SetHintRow(3, "AM_CopyLastPad", "Copy last marker");
			else
				SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else if (est == 1003)	// звичайний GM — KB/M
		{
			SM_SetHintRow(0, "AM_MarkerMove",     "Move marker");
			SM_SetHintRow(1, "AM_MapAction",       "Double-click — edit");
			SM_SetHintRow(2, "AM_Delete", "Remove");
			if (SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
				SM_SetHintRow(3, "AM_CopyLast",    "Copy last marker");
			else
				SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
		else	// 1000 діалог / 1001 видимість off — ховаємо всі
		{
			SM_SetHintRowVisible(0, false);
			SM_SetHintRowVisible(1, false);
			SM_SetHintRowVisible(2, false);
			SM_SetHintRowVisible(3, false);
			SM_SetHintRowVisible(4, false);
		}
	}

	// Налаштувати рядок підказки: гліф дії + підпис. Пасивний (без звуку/анімації/реакції на дію).
	protected void SM_SetHintRow(int idx, string action, string label)
	{
		if (idx < 0 || idx >= m_aSMHintRows.Count() || !m_aSMHintRows[idx])
			return;
		m_aSMHintRows[idx].SetVisible(true);
		SCR_InputButtonComponent comp = SCR_InputButtonComponent.Cast(m_aSMHintRows[idx].FindHandler(SCR_InputButtonComponent));
		if (!comp)
			return;
		comp.SetAction(action);
		comp.SetLabel(label);
		comp.SetClickSoundDisabled(true);
		comp.SM_MakeDisplayOnly();	// прибрати слухачів дії — підказка не блимає при кліках на мапі
	}

	protected void SM_SetHintRowVisible(int idx, bool vis)
	{
		if (idx < 0 || idx >= m_aSMHintRows.Count() || !m_aSMHintRows[idx])
			return;
		m_aSMHintRows[idx].SetVisible(vis);
	}

	// Тултіп «Edited by: <нік>» при наведенні на мітку. Перерахунок лише коли курсор рухається.
	protected void SM_UpdateTooltip()
	{
		if (!m_wSMTooltip)
			return;

		// Не показуємо під час діалогу/перенесення
		if (m_MarkerEditRoot || m_iSMCarryId != -1)
		{
			SM_HideTooltip();
			return;
		}

		// Троттл по СВІТОВІЙ точці під курсором: змінюється і коли рухаємо курсор, і коли панорамуємо
		// (лівий стік — курсор на екрані нерухомий, а світова точка під ним інша).
		int twx, twy;
		SM_GetCursorWorld(twx, twy);
		if (twx == m_iSMTipWX && twy == m_iSMTipWY)
			return;
		m_iSMTipWX = twx;
		m_iSMTipWY = twy;

		SM_MarkerVisual vis = SM_FindMarkerUnderCursor();
		if (!vis || !vis.m_Data)
		{
			SM_UpdateStrokeTooltip();	// мітки нема — може, під курсором штрих малюнка (автор + видимість)
			return;
		}
		SM_MapMarkerData d = vis.m_Data;

		// Зевс позначив мітку «Hide info» — для гравців (поза редактором) не показуємо тултіп взагалі.
		// У GM-мапі зевс інфо однаково бачить.
		if (d.m_iHideInfo != 0 && !SM_IsEditorMap())
		{
			SM_HideTooltip();
			return;
		}

		float x = SCR_MapCursorInfo.x + 18;
		float y = SCR_MapCursorInfo.y + 34;

		// Рядок «хто» — ім'я зберігається в JSON, тож показуємо й після рестарту. Якщо невідоме — ховаємо лише цей рядок.
		if (d.m_sLastEditor != "")
		{
			m_wSMTooltip.SetText("Edited by: " + d.m_sLastEditor);
			m_wSMTooltip.SetVisible(true);
			FrameSlot.SetPos(m_wSMTooltip, x, y);
			y += 20;	// видимість піде нижче
		}
		else
		{
			m_wSMTooltip.SetVisible(false);
		}

		// Рядок видимості — показуємо ЗАВЖДИ (m_iVisibility зберігається), колір за областю.
		if (m_wSMTooltipVis)
		{
			string visLabel;
			int visColor;
			SM_VisibilityLabel(d.m_iVisibility, visLabel, visColor);
			// У GM-мапі для Side-міток дописуємо назву сторони (BLUFOR/OPFOR/INDFOR), щоб зевс її розрізняв.
			if (SM_IsEditorMap() && d.m_iVisibility == SM_EMarkerVisibility.FACTION)
			{
				string side = SM_FactionSideName(d.m_iChannel);
				if (side != "")
				{
					visLabel = visLabel + " · " + side;
					visColor = SM_FactionSideColor(d.m_iChannel);	// колір за стороною
				}
			}
			m_wSMTooltipVis.SetText(visLabel);
			m_wSMTooltipVis.SetColor(Color.FromInt(visColor));
			m_wSMTooltipVis.SetVisible(true);
			FrameSlot.SetPos(m_wSMTooltipVis, x, y);
		}
	}

	protected void SM_HideTooltip()
	{
		if (m_wSMTooltip)
			m_wSMTooltip.SetVisible(false);
		if (m_wSMTooltipVis)
			m_wSMTooltipVis.SetVisible(false);
	}

	// Тултіп для ШТРИХА малюнка під курсором: «Drawn by» (за конфігом showLastEditor) + видимість.
	// Викликається з SM_UpdateTooltip, коли мітки під курсором нема (троттл по руху курсора вже пройдено).
	protected void SM_UpdateStrokeTooltip()
	{
		if (!m_DrawCanvas)
		{
			SM_HideTooltip();
			return;
		}
		// GM-мапа з ВИМКНЕНИМ показом малюнків: штрихів не видно — тултіп по «невидимому» не показуємо.
		if (SM_IsEditorMap() && !SM_GmState.s_bDrawView)
		{
			SM_HideTooltip();
			return;
		}
		// Під час малювання/стирання тултіп не заважає.
		if (m_DrawCanvas.IsActive() && m_bSMDrawDown)
		{
			SM_HideTooltip();
			return;
		}

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
		{
			SM_HideTooltip();
			return;
		}
		int ttwx, ttwy;
		bool ttHaveW = SM_GetCursorWorld(ttwx, ttwy);
		int cpx, cpy;
		SM_CursorPhysPx(cpx, cpy);
		int strokeId = m_DrawCanvas.FindStrokeAtScreen(cpx, cpy, ttwx, ttwy, ttHaveW);
		if (strokeId == -1)	// -1 = нічого не влучили (Local-штрихи мають негативні id <= -2 — теж валідні)
		{
			SM_HideTooltip();
			return;
		}
		SM_MapDrawingData sd = SM_DrawCanvas.GetStrokeData(strokeId);
		if (!sd)
		{
			SM_HideTooltip();
			return;
		}

		// Зевс позначив штрих «Hide info» — гравцям (поза редактором) тултіп не показуємо взагалі.
		if (sd.m_iHideInfo != 0 && !SM_IsEditorMap())
		{
			SM_HideTooltip();
			return;
		}

		float x = SCR_MapCursorInfo.x + 18;
		float y = SCR_MapCursorInfo.y + 34;

		if (sd.m_sOwnerName != "" && SM_MarkerConfig.GetInstance().m_bShowLastEditor)
		{
			m_wSMTooltip.SetText("Drawn by: " + sd.m_sOwnerName);
			m_wSMTooltip.SetVisible(true);
			FrameSlot.SetPos(m_wSMTooltip, x, y);
			y += 20;
		}
		else
		{
			m_wSMTooltip.SetVisible(false);
		}

		if (m_wSMTooltipVis)
		{
			string visLabel;
			int visColor;
			SM_VisibilityLabel(sd.m_iVisibility, visLabel, visColor);
			// У GM-мапі для Side-штрихів дописуємо сторону (BLUFOR/OPFOR/INDFOR) — як у міток.
			if (SM_IsEditorMap() && sd.m_iVisibility == SM_EMarkerVisibility.FACTION)
			{
				string side = SM_FactionSideName(sd.m_iChannel);
				if (side != "")
				{
					visLabel = visLabel + " · " + side;
					visColor = SM_FactionSideColor(sd.m_iChannel);
				}
			}
			m_wSMTooltipVis.SetText(visLabel);
			m_wSMTooltipVis.SetColor(Color.FromInt(visColor));
			m_wSMTooltipVis.SetVisible(true);
			FrameSlot.SetPos(m_wSMTooltipVis, x, y);
		}
	}

	// Підпис і колір рядка видимості: Local-сірий / Group-зелений / Side-синій / Global-червоний.
	protected void SM_VisibilityLabel(int vis, out string label, out int color)
	{
		switch (vis)
		{
			case SM_EMarkerVisibility.PERSONAL: label = "Local";  color = 0xFFAAAAAA; return;	// сірий
			case SM_EMarkerVisibility.GROUP:    label = "Group";  color = 0xFF49C24A; return;	// зелений
			case SM_EMarkerVisibility.FACTION:  label = "Side";   color = 0xFF2E6FE6; return;	// синій
			case SM_EMarkerVisibility.ALL:      label = "Global"; color = 0xFFD83A3A; return;	// червоний
		}
		label = "";
		color = 0xFFFFFFFF;
	}

	// 3. ВВІД
	// Гейтинг: мапа готова прийняти наш клік (не сфокусована панель, не popup-режим).
	protected bool SM_CanAcceptMapClick()
	{
		if (m_MarkerEditRoot)		// відкритий діалог — кліки по мапі ігноруємо
			return false;

		if (IsToolMenuFocused())
			return false;

		// CS_PAN виключаємо з маски: на паді ліво-стіком панорамуєш мапу й водночас хочеш
		// наводитись/видаляти. Решту popup-обмежень (radial/draw/rotate/команди) лишаємо.
		if (m_CursorModule && (m_CursorModule.GetCursorState() & (SCR_MapCursorModule.STATE_POPUP_RESTRICTED & ~EMapCursorState.CS_PAN)) != 0)
			return false;

		return true;
	}

	//! Утримання, доведене до порогу. The engine runs the clock (InputFilterHold on the action), which
	//! is what makes the hint fill its own bar and what lets a player put either hold on a key of its
	//! own. Both default to AM_MapAction's button, and which one is meant is settled by what sits
	//! under the cursor: a marker means carry it, bare map means point at it.
	protected bool SM_HoldMove(InputManager im)
	{
		return im.GetActionValue("AM_MarkerMove") > 0.5;
	}

	protected bool SM_HoldPoint(InputManager im)
	{
		return im.GetActionValue("AM_Pointer") > 0.5;
	}

	//! Що лежить під утриманням: id мітки, або -1 (гола мапа).
	//!
	//! When the hold shares the map button — the default — it belongs to that press: honour the marker
	//! it started on, and the guard that lets you drag away to cancel it. On a key of its own there is
	//! no press to belong to, so read whatever the cursor sits on right now.
	protected int SM_HoldTarget(bool mapActionDown)
	{
		if (mapActionDown)
		{
			if (m_iSMPressMarkerId != -1 && SM_CursorNearPress())
				return m_iSMPressMarkerId;
			return -1;
		}

		SM_MarkerVisual u = SM_FindMarkerUnderCursor();
		if (u && u.m_Data)
			return u.m_Data.m_iId;
		return -1;
	}

	protected void SM_StopPointing()
	{
		m_bSMPointing = false;
		if (m_CursorModule)
			m_CursorModule.HandleDialog(false);	// повертаємо інфо-текст
		SM_SetMapCursorHidden(false);			// повертаємо віджет курсора

		SCR_PlayerController lpc = SM_LocalPC();
		if (lpc)
		{
			SM_PointerHub.GetInstance().Hide(lpc.GetPlayerId());
			lpc.SM_RequestPointStop();
		}
	}

	// Повний поллінг AM_MapAction (фронти сигналу). Імунний до повторних/миттєвих
	// MapSelect DOWN/UP. Натиск → запам'ятати; утримання на мітці → підняти; відпуск → поставити
	// (якщо тягнемо) АБО подвійний клік (редагувати/створити).
	// Розміщення мітки зевсом у GM-мапі: після кнопки Create Marker наступний клік по мапі задає
	// точку й відкриває наш діалог. Повний ввід (переміщення/редагування) тут поки не обробляємо.
	protected void SM_PollEditorPlacement(InputManager im)
	{
		bool down = im.GetActionValue("AM_MapAction") > 0.5;
		float now = System.GetTickCount() / 1000.0;

		if (SM_GmState.s_bCreatePending)
		{
			if (!m_bSMWasCreatePending)	// щойно увійшли в режим (натиснули кнопку)
			{
				m_bSMWasCreatePending = true;
				m_bSMCreateSawRelease = false;	// спершу дочекаємось відпускання кліку по кнопці
			}

			if (!m_MarkerEditRoot)
			{
				if (!m_bSMCreateSawRelease)
				{
					if (!down)
						m_bSMCreateSawRelease = true;	// кнопку відпущено — далі ловимо клік по мапі
				}
				else if (!down && m_bSMLmbDown)	// справжній клік по мапі завершено
				{
					int wx, wy;
					if (SM_GetCursorWorld(wx, wy))
					{
						SM_GmState.s_bCreatePending = false;
						SM_OpenCreate(wx, wy);
					}
				}
			}
		}
		else if (SM_GmState.s_bMarkerView && !m_MarkerEditRoot)
		{
			m_bSMWasCreatePending = false;

			// --- ФРОНТ ВНИЗ: запам'ятати мітку під курсором ---
			if (down && !m_bSMLmbDown)
			{
				m_fSMPressTime = now;
				m_fSMPressX = SCR_MapCursorInfo.x;
				m_fSMPressY = SCR_MapCursorInfo.y;
				m_bSMPickedThisPress = false;
				m_iSMPressMarkerId = -1;
				SM_MarkerVisual u = SM_FindMarkerUnderCursor();
				if (u && u.m_Data)
					m_iSMPressMarkerId = u.m_Data.m_iId;
			}

			// --- УТРИМАННЯ на мітці ≥ HOLD → підняти (тягнути) ---
			if (down && m_iSMCarryId == -1 && m_iSMPressMarkerId != -1
				&& SM_HoldMove(im) && SM_CursorNearPress())
			{
				m_iSMCarryId = m_iSMPressMarkerId;	// у редакторі зевс — lock його не стримує
				m_bSMPickedThisPress = true;		// відпуск ЦЬОГО утримання не рахуємо як клік
			}

			// --- ФРОНТ ВГОРУ ---
			if (!down && m_bSMLmbDown)
			{
				if (m_bSMPickedThisPress)
				{
					m_bSMPickedThisPress = false;	// це відпуск підняття — мітка лишається "в руці"
				}
				else if (m_iSMCarryId != -1)
				{
					int wx, wy;
					if (SM_GetCursorWorld(wx, wy))
						SM_DoMoveMarker(m_iSMCarryId, wx, wy);	// commit the move (Local or server)
					m_iSMCarryId = -1;
					m_iSMPressMarkerId = -1;
				}
			}
		}
		else
		{
			m_bSMWasCreatePending = false;
		}

		m_bSMLmbDown = down;
	}

	// Захоплення штриха в режимі малювання: натиск/перетяг/відпуск ЛКМ → полотно.
	// Клік над панеллю параметрів не починає штрих.
	protected void SM_PollDraw(InputManager im)
	{
		bool down = im.GetActionValue("AM_MapAction") > 0.5;

		// Курсор над панеллю? (фіз. px = DPI-scale від unscaled-курсора мапи)
		WorkspaceWidget ws = GetGame().GetWorkspace();
		bool overPanel = false;
		if (m_DrawPanel && ws)
		{
			int opx, opy;
			SM_CursorPhysPx(opx, opy);	// panel hit-tests against widget screen positions
			overPanel = m_DrawPanel.IsCursorOver(opx, opy);
		}

		int wx, wy;
		bool haveWorld = SM_GetCursorWorld(wx, wy);

		// Shift chains straight segments into one polyline: each click continues from the end of the
		// previous one, and letting Shift go commits the whole thing as a single stroke.
		bool lineMode = im.GetActionValue("AM_LineModifier") > 0.5;

		if (down && !m_bSMDrawDown)
		{
			if (haveWorld && !overPanel)
				m_DrawCanvas.OnPressDown(wx, wy, lineMode);
		}
		else if (down && m_bSMDrawDown)
		{
			if (haveWorld)
				m_DrawCanvas.OnDrag(wx, wy);
		}
		else if (!down && m_bSMDrawDown)
		{
			if (haveWorld)
				m_DrawCanvas.OnRelease(wx, wy, lineMode);
		}
		else if (m_DrawCanvas.HasLineChain())
		{
			// LMB up with a chain open: trail the rubber band until Shift is released.
			if (!lineMode)
				m_DrawCanvas.FinishLineChain();
			else if (haveWorld)
				m_DrawCanvas.OnLineChainHover(wx, wy);
		}
		m_bSMDrawDown = down;
	}

	protected void SM_PollMouse()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		// A template modal is up (typing the name, or the delete confirmation): hands off. Our polling
		// re-reads the mouse every frame and would knock the edit box out of write mode — and a click
		// meant for a dialog button must not land on the map underneath it.
		if (m_DrawPanel && m_DrawPanel.IsModalBusy())
			return;

		// Режим малювання перехоплює ЛКМ повністю (без логіки міток). ПЕРЕД editor-гілкою:
		// зевс теж малює (інструмент вмикається лише з відкритою панеллю «Drawing tools»).
		if (m_DrawCanvas && m_DrawCanvas.IsActive() && im.IsUsingMouseAndKeyboard())
		{
			SM_PollDraw(im);
			return;
		}

		// У GM-мапі повний клік-ввід не обробляємо (щоб не заважати редактору), лише режим розміщення
		// мітки після кнопки Create Marker.
		if (SM_IsEditorMap())
		{
			SM_PollEditorPlacement(im);
			return;
		}

		// Геймпад (консоль): окрема дискретна модель — мишачі жести (подвійний клік, hold-drag) не
		// переносяться на стік. Курсор той самий (SCR_MapCursorInfo стік-керований), кнопки — наші екшени.
		if (!im.IsUsingMouseAndKeyboard())
		{
			SM_PollGamepad(im);
			return;
		}

		// Курсор над панеллю малювання (чи її відкритим списком) — кліки належать панелі:
		// не даємо марковській логіці (подвійний клік create / hold / вказівник) спрацювати «крізь» неї.
		if (m_DrawPanel)
		{
			int ppx, ppy;
			SM_CursorPhysPx(ppx, ppy);
			if (m_DrawPanel.IsCursorOver(ppx, ppy))
			{
				m_bSMLmbDown = im.GetActionValue("AM_MapAction") > 0.5;	// тримаємо стан ЛКМ актуальним
				m_iSMPressMarkerId = -1;
				m_bSMPickedThisPress = false;
				return;
			}
		}

		bool down = im.GetActionValue("AM_MapAction") > 0.5;
		float now = System.GetTickCount() / 1000.0;

		if (SM_CanAcceptMapClick())
		{
			// --- ФРОНТ ВНИЗ (натиск) — запам'ятати позицію/мітку ---
			if (down && !m_bSMLmbDown)
			{
				m_fSMPressTime = now;
				m_fSMPressX = SCR_MapCursorInfo.x;
				m_fSMPressY = SCR_MapCursorInfo.y;
				m_bSMPickedThisPress = false;
				m_iSMPressMarkerId = -1;
				if (SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))	// without tools a marker is not a pick target
				{
					SM_MarkerVisual u = SM_FindMarkerUnderCursor();
					if (u && u.m_Data)
						m_iSMPressMarkerId = u.m_Data.m_iId;
				}
			}

			// Обидва утримання йдуть від СВОГО фронту, а не від ЛКМ: інакше перебінд «нести» чи
			// «вказувати» на іншу клавішу робив би з них модифікатор до лівої кнопки.
			bool holdMove  = SM_HoldMove(im);
			bool holdPoint = SM_HoldPoint(im);

			// --- УТРИМАННЯ на мітці → підняти ---
			if (holdMove && !m_bSMHoldMoveDown && m_iSMCarryId == -1 && !m_bSMPointing
				&& SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
			{
				int pickId = SM_HoldTarget(down);
				if (pickId != -1)
				{
					SM_MapMarkerData pm = SM_MapMarkerStore.GetInstance().FindById(pickId);
					if (SM_BlockedByLock(pm))
					{
						if (down)
							m_bSMPickedThisPress = true;	// споживаємо утримання (не піднімаємо й не ставимо)
						m_iSMPressMarkerId = -1;			// щоб повідомлення не повторювалось щокадру
					}
					else
					{
						m_iSMCarryId = pickId;
						if (down)
							m_bSMPickedThisPress = true;	// відпуск ЦЬОГО натиску НЕ ставитиме мітку
					}
				}
			}
			m_bSMHoldMoveDown = holdMove;

			// --- УТРИМАННЯ на ПУСТОМУ місці → режим «вказівник» (показати пальцем) ---
			if (holdPoint && !m_bSMHoldPointDown && m_iSMCarryId == -1 && !m_bSMPointing
				&& SM_HoldTarget(down) == -1
				&& SM_HasFeature(AM_EMapFeature.POINTER)
				&& SM_MarkerConfig.GetInstance().m_bAllowPointer)	// вимкнено в конфізі — не входимо в режим
			{
				m_bSMPointing = true;
				if (down)
					m_bSMPickedThisPress = true;	// відпуск НЕ створюватиме мітку
				if (m_CursorModule)
					m_CursorModule.HandleDialog(true);	// ховає інфо-текст координат/висоти
				SM_SetMapCursorHidden(true);			// ховає сам віджет курсора карти
			}
			m_bSMHoldPointDown = holdPoint;

			// --- вказівник відпустили ---
			if (m_bSMPointing && !holdPoint)
				SM_StopPointing();

			// --- поки водимо вказівником: своя точка локально + потік позиції на сервер (троттл) ---
			if (m_bSMPointing)
			{
				SM_SetMapCursorHidden(true);	// тримаємо курсор схованим, якщо модуль його повертає
				int px, py;
				if (SM_GetCursorWorld(px, py))
				{
					SCR_PlayerController lpc = SM_LocalPC();
					if (lpc)
					{
						SM_PointerHub.GetInstance().Show(lpc.GetPlayerId(), px, py, now);
						if (now - m_fSMLastPointSend >= SM_POINT_SEND)
						{
							lpc.SM_RequestPointUpdate(px, py);
							m_fSMLastPointSend = now;
						}
					}
				}
			}

			// --- ФРОНТ ВГОРУ (відпуск) ---
			if (!down && m_bSMLmbDown)
			{
				if (m_bSMPickedThisPress)
				{
					// це відпуск утримання-підняття → мітка лишається «в руці», ставимо наступним кліком
					m_bSMPickedThisPress = false;
				}
				else if (m_iSMCarryId != -1)
				{
					// поставити (підтвердити переміщення)
					int wx, wy;
					if (SM_GetCursorWorld(wx, wy))
						SM_DoMoveMarker(m_iSMCarryId, wx, wy);	// Local or server
					m_iSMCarryId = -1;
					m_iSMPressMarkerId = -1;
				}
			}
		}

		m_bSMLmbDown = down;	// стан для наступного кадру (фронти)

		// --- ПКМ по військовому пресету (коли діалог відкритий) → видалити (вбудовані захищені) ---
		bool rdown = im.GetActionValue("AM_Cancel") > 0.5;
		if (rdown && !m_bSMRmbDown && m_MarkerEditRoot)
		{
			if (m_iSMHoveredMilPreset >= 0)
			{
				SM_MapMarkerPresets.GetInstance().RemoveMilitary(m_iSMHoveredMilPreset);
				m_iSMHoveredMilPreset = -1;
				SM_WirePresets();
			}
			else if (m_iSMHoveredGenPreset >= 0)
			{
				SM_MapMarkerPresets.GetInstance().RemoveGeneral(m_iSMHoveredGenPreset);
				m_iSMHoveredGenPreset = -1;
				SM_WirePresets();
			}
		}
		m_bSMRmbDown = rdown;
	}

	// Поллінг вводу з геймпада (консоль). Дискретна модель замість мишачих жестів.
	// A (AM_MapAction) контекстна — на мітці відкриває редагування, на пустому місці створює.
	// Курсор той самий (SCR_MapCursorInfo). Move/copy/вказівник додамо наступними кроками.
	protected void SM_PollGamepad(InputManager im)
	{
		WorkspaceWidget gws = GetGame().GetWorkspace();
		Widget focused = null;
		if (gws)
			focused = gws.GetFocusedWidget();
		bool focusInPanel = (focused && m_DrawPanel && m_DrawPanel.ContainsWidget(focused));

		// Пад-малювання: інструмент (олівець/гумка) активний → A затиснутим малює/стирає по курсору
		// (стік веде), відпускання — коміт. B — скасувати інструмент (вийти в звичайний режим),
		// X — видалити штрих під курсором. Мітко-дії A/Y недоступні, поки інструмент активний.
		//
		// This comes BEFORE the focus guard on purpose. A host screen (a tablet menu) always keeps SOME
		// widget focused, so guarding first meant the pad could never draw there at all — A was
		// swallowed every frame. An armed tool outranks a focused widget; only OUR panel still wins, so
		// A keeps operating the panel while the pad is inside it.
		if (m_DrawCanvas && m_DrawCanvas.IsActive() && !focusInPanel)
		{
			// An armed tool takes the pad exclusively: drop any focus the host left lying around, or
			// the same A press would both draw AND activate whatever widget happened to be focused.
			if (focused && gws)
			{
				gws.SetFocusedWidget(null);
				focused = null;
			}

			bool draw = im.GetActionValue("AM_MapAction") > 0.5;	// A
			int dwx, dwy;
			bool haveW = SM_GetCursorWorld(dwx, dwy);
			if (draw && !m_bSMPadDrawDown)
			{
				if (haveW)
					m_DrawCanvas.OnPressDown(dwx, dwy);
			}
			else if (draw && m_bSMPadDrawDown)
			{
				if (haveW)
					m_DrawCanvas.OnDrag(dwx, dwy);
			}
			else if (!draw && m_bSMPadDrawDown)
			{
				if (haveW)
					m_DrawCanvas.OnRelease(dwx, dwy);
			}
			m_bSMPadDrawDown = draw;

			// Template tools answer to the AM_Tpl* listeners (B steps the flow back through the panel's
			// Cancel, X belongs to Remove) — the generic tool-off and stroke-delete must stand down, or
			// one B press would both un-anchor the ghost AND drop the tool.
			int padTool = m_DrawCanvas.GetTool();
			bool tplTool = (padTool == SM_DrawCanvas.TOOL_TEMPLATE || padTool == SM_DrawCanvas.TOOL_SELECT);

			// B — скасувати інструмент
			bool cancel = im.GetActionValue("AM_Cancel") > 0.5 || im.GetActionValue("MenuBack") > 0.5;
			if (cancel && !m_bSMPadCancelDown && !tplTool)
				m_DrawCanvas.SetActive(false);
			m_bSMPadCancelDown = cancel;

			// X — видалити штрих під курсором
			bool delx = im.GetActionValue("AM_Delete") > 0.5;
			if (delx && !m_bSMPadDeleteDown && !tplTool)
				SM_TryDeleteStrokeAtCursor();
			m_bSMPadDeleteDown = delx;

			// Мітко-стан A тримаємо «спожитим», щоб після B-скасування відпуск A не створив мітку.
			m_bSMPadConfirmDown = draw;
			if (draw)
				m_bSMPickedThisPress = true;
			return;
		}
		else
		{
			// CONSUME the current state, never blank it. Blanking meant a button still HELD from before
			// the tool armed read as a fresh press the moment it did: pressing A on the panel's Place
			// armed a shape and the very same held A immediately dropped its first point wherever the
			// cursor happened to sit — the centre of the screen. Now the press must be released first.
			m_bSMPadDrawDown = im.GetActionValue("AM_MapAction") > 0.5;
			m_bSMPadCancelDown = im.GetActionValue("AM_Cancel") > 0.5 || im.GetActionValue("MenuBack") > 0.5;
		}

		// Фокус на UI → пад взаємодіє з тим UI, а не з мапою: A/Y/X тут мовчать. Інакше вибір елемента
		// ванільного меню мапи кнопкою A одночасно ставив би мітку (баг «компас + мітка»).
		//
		// But that heuristic — "something is focused, therefore the pad is talking to UI" — is simply
		// FALSE on a host screen: a tablet menu keeps a widget focused at all times, so the guard used
		// to swallow every A press forever and the pointer could never fire there. Where the map does
		// not own the screen, only OUR panel gets to claim the pad.
		bool hostScreen = !SM_HasFeature(AM_EMapFeature.MARKER_TOOLS);
		if (focused && (focusInPanel || !hostScreen))
		{
			// «Проковтнути» поточні натиски: стани = down, щоб після зняття фокуса (вибір у меню
			// кнопкою A) той САМИЙ натиск не спрацював фронтом як тап/дія на мапі. Додатково
			// позначаємо прес «спожитим»: release-гілка A інакше зробила б create/edit на відпусканні.
			m_bSMPadConfirmDown = im.GetActionValue("AM_MapAction") > 0.5;
			if (m_bSMPadConfirmDown)
				m_bSMPickedThisPress = true;
			m_bSMPadPlaceDown   = im.GetActionValue("AM_CopyLastPad") > 0.5;
			m_bSMPadDeleteDown  = im.GetActionValue("AM_Delete")  > 0.5;
			return;
		}

		// The templates tab is open and the pad is on the map: A/B/Y/X belong to the template flow
		// (the AM_Tpl* listeners run it). Marker tap/create, copy-last and delete must not fire
		// underneath the same press.
		if (m_DrawPanel && m_DrawPanel.IsTemplatesOpen())
		{
			m_bSMPadConfirmDown = im.GetActionValue("AM_MapAction") > 0.5;
			if (m_bSMPadConfirmDown)
				m_bSMPickedThisPress = true;
			m_bSMPadPlaceDown  = im.GetActionValue("AM_CopyLastPad") > 0.5;
			m_bSMPadDeleteDown = im.GetActionValue("AM_Delete") > 0.5;
			return;
		}

		bool confirm = im.GetActionValue("AM_MapAction") > 0.5;	// A — тап: редаг/створ; утримання: нести/вказувати
		bool place   = im.GetActionValue("AM_CopyLastPad") > 0.5;	// Y — копія останньої мітки
		bool del     = im.GetActionValue("AM_Delete")  > 0.5;	// X — видалити мітку під курсором
		float now = System.GetTickCount() / 1000.0;

		bool placeEdge = place && !m_bSMPadPlaceDown;
		bool delEdge   = del   && !m_bSMPadDeleteDown;

		if (SM_CanAcceptMapClick())
		{
			// --- Y: копія останньої мітки в точку курсора ---
			if (placeEdge && m_iSMCarryId == -1 && !m_bSMPointing
				&& SM_HasFeature(AM_EMapFeature.MARKER_TOOLS)
				&& m_SMLastTemplate && SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
			{
				SM_PlaceCopyAtCursor();
			}
			// --- X: видалити мітку під курсором (залочену зевсом не чіпаємо); нема мітки — штрих малюнка ---
			else if (delEdge && m_iSMCarryId == -1 && !m_bSMPointing)
			{
				SM_MarkerVisual du = null;
				if (SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
					du = SM_FindMarkerUnderCursor();
				if (du && du.m_Data && !SM_BlockedByLock(du.m_Data))
				{
					SM_DeleteMarkerById(du.m_Data.m_iId);	// Local or server
				}
				else if ((!du || !du.m_Data) && m_DrawPanel)	// stroke erase = drawing tools feature
				{
					SM_TryDeleteStrokeAtCursor();
				}
			}

			// --- A фронт ВНИЗ: запам'ятати позицію/мітку під курсором ---
			if (confirm && !m_bSMPadConfirmDown)
			{
				m_fSMPressTime = now;
				m_fSMPressX = SCR_MapCursorInfo.x;
				m_fSMPressY = SCR_MapCursorInfo.y;
				m_bSMPickedThisPress = false;
				m_iSMPressMarkerId = -1;
				if (SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))	// without tools a marker is not a pick target
				{
					SM_MarkerVisual pu = SM_FindMarkerUnderCursor();
					if (pu && pu.m_Data)
						m_iSMPressMarkerId = pu.m_Data.m_iId;
				}
			}

			// --- A УТРИМАННЯ на мітці ≥ HOLD → підняти (далі слідує за курсором; відпуск поставить) ---
			if (confirm && m_iSMCarryId == -1 && m_iSMPressMarkerId != -1
				&& SM_HoldMove(im) && SM_CursorNearPress())
			{
				SM_MapMarkerData pm = SM_MapMarkerStore.GetInstance().FindById(m_iSMPressMarkerId);
				if (SM_BlockedByLock(pm))
				{
					m_bSMPickedThisPress = true;	// залочена — споживаємо утримання, не піднімаємо
					m_iSMPressMarkerId = -1;
				}
				else
				{
					m_iSMCarryId = m_iSMPressMarkerId;
					m_bSMPickedThisPress = true;
				}
			}

			// --- A УТРИМАННЯ на ПУСТОМУ ≥ SM_POINT_HOLD_SEC → вказівник (показати пальцем) ---
			if (confirm && m_iSMCarryId == -1 && !m_bSMPointing && m_iSMPressMarkerId == -1
				&& SM_HoldPoint(im)
				&& SM_HasFeature(AM_EMapFeature.POINTER)
				&& SM_MarkerConfig.GetInstance().m_bAllowPointer)
			{
				m_bSMPointing = true;
				m_bSMPickedThisPress = true;
				// НЕ кличемо HandleDialog(true): він ставить CS_DIALOG, що блокує пан — а нам треба,
				// щоб лівий стік панорамував і так водив пальцем. Ванільний курсор ховаємо власним методом.
				SM_SetMapCursorHidden(true);
			}

			// --- поки вказуємо: своя точка локально + потік позиції на сервер (троттл) ---
			if (confirm && m_bSMPointing)
			{
				SM_SetMapCursorHidden(true);
				int px, py;
				if (SM_GetCursorWorld(px, py))
				{
					SCR_PlayerController lpc = SM_LocalPC();
					if (lpc)
					{
						SM_PointerHub.GetInstance().Show(lpc.GetPlayerId(), px, py, now);
						if (now - m_fSMLastPointSend >= SM_POINT_SEND)
						{
							lpc.SM_RequestPointUpdate(px, py);
							m_fSMLastPointSend = now;
						}
					}
				}
			}

			// --- A фронт ВГОРУ: завершити вказівник / поставити перенесену / швидкий тап = редаг/створ ---
			if (!confirm && m_bSMPadConfirmDown)
			{
				if (m_bSMPointing)
				{
					m_bSMPointing = false;
					SM_SetMapCursorHidden(false);
					SCR_PlayerController lpc = SM_LocalPC();
					if (lpc)
					{
						SM_PointerHub.GetInstance().Hide(lpc.GetPlayerId());
						lpc.SM_RequestPointStop();
					}
				}
				else if (m_iSMCarryId != -1)
				{
					int wx, wy;
					if (SM_GetCursorWorld(wx, wy))
					{
						SCR_PlayerController mpc = SM_LocalPC();
						if (mpc)
							SM_DoMoveMarker(m_iSMCarryId, wx, wy);	// commit the move (Local or server)
					}
					m_iSMCarryId = -1;
				}
				else if (!m_bSMPickedThisPress && SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
				{
					// швидкий тап (без утримання) → на мітці редагувати, на пустому створити (відкриваємо на UP)
					SM_MarkerVisual u = SM_FindMarkerUnderCursor();
					if (u && u.m_Data)
						SM_OpenEdit(u.m_Data);
					else
					{
						int wx, wy;
						if (SM_GetCursorWorld(wx, wy))
							SM_OpenCreate(wx, wy);
					}
				}
				m_bSMPickedThisPress = false;
				m_iSMPressMarkerId = -1;
			}
		}

		m_bSMPadConfirmDown = confirm;
		m_bSMPadPlaceDown   = place;
		m_bSMPadDeleteDown  = del;
	}

	// ПКМ: скасувати переміщення (мітка повертається на місце; на сервер нічого не шлемо).
	protected void SM_OnContext(float value, EActionTrigger reason)
	{
		if (SM_TryPanelBack())	// пад-B у панелі часто приходить як AM_Cancel (фокус на нашій STOP-кнопці)
			return;
		if (SM_TryTemplateBack())
			return;
		if (m_iSMCarryId != -1)
			m_iSMCarryId = -1;	// наступний кадр позиціонує з m_Data (оригінал)
	}

	// Пад: хрестовина вправо — зайти фокусом у панель малювання (перший елемент) / вийти з неї.
	// Поки фокус усередині панелі, пад-дії мапи (A/Y/X) мовчать (гейт у SM_PollGamepad),
	// тож вибір елемента НЕ ставить мітку. Зевсу поки не даємо (окрема задача).
	//! RB on the pad. On the normal map the panel is always up, so this just moves focus in and out of
	//! it. On a host screen that starts our panel hidden (a tablet), RB is the whole entry point: one
	//! press opens it AND puts the pad in it, a second press closes it again — otherwise a console
	//! player would have to steer the cursor onto the tablet's pencil, which is exactly what the
	//! button exists to avoid.
	protected void SM_OnPanelFocus(float value, EActionTrigger reason)
	{
		if (!m_bSMMapOpen || m_MarkerEditRoot || !m_DrawPanel || SM_IsEditorMap())
			return;
		InputManager pim = GetGame().GetInputManager();
		if (!pim)
			return;

		WorkspaceWidget ws = GetGame().GetWorkspace();
		if (!ws)
			return;

		// On mouse and keyboard the panel is just shown or put away — there is a cursor to click it
		// with, so nothing is gained by moving focus into it. Only the pad needs to be walked in.
		if (pim.IsUsingMouseAndKeyboard())
		{
			AM_ToggleDrawPanel();
			return;
		}

		if (!m_bSMDrawPanelShown)
		{
			AM_SetDrawPanelShown(true);
			m_DrawPanel.SetVisible(true);	// now, not next Update — we focus it on this very frame
		}

		Widget f = ws.GetFocusedWidget();
		if (f && m_DrawPanel.ContainsWidget(f))
		{
			SM_PanelExit();	// повторний RB — вихід із меню
			if (m_bSMPanelStartsHidden)
				AM_SetDrawPanelShown(false);	// host screen owns the panel: put it away again
			return;
		}
		Widget first = m_DrawPanel.GetFirstFocusTarget();
		if (first)
		{
			m_bSMPanelPadNav = true;	// свідомий вхід (щоб анти-автофокус в Update не скинув)
			m_DrawPanel.SetPadFocusMode(true);	// зробити кнопки фокусними на час входу
			ws.SetFocusedWidget(first);	// зайти в панель: фокус + підсвітка першого елемента
			m_DrawPanel.NotifyPadEntered();	// «проковтнути» цей натиск для навігації
		}
	}

	// Видалити штрих малюнка під курсором (спільне для Del/пад-X): залочений зевсом —
	// локальне повідомлення без RPC, інакше запит на сервер (він перевірить права).
	protected void SM_TryDeleteStrokeAtCursor()
	{
		if (!m_DrawCanvas)
			return;
		WorkspaceWidget xws = GetGame().GetWorkspace();
		if (!xws)
			return;
		int dwx, dwy;
		bool dHaveW = SM_GetCursorWorld(dwx, dwy);
		int cpx, cpy;
		SM_CursorPhysPx(cpx, cpy);
		int strokeId = m_DrawCanvas.FindStrokeAtScreen(cpx, cpy, dwx, dwy, dHaveW);
		if (strokeId == -1)
			return;
		SCR_PlayerController spc = SM_LocalPC();
		if (!spc)
			return;
		SM_DeleteStrokeById(strokeId, spc);
	}

	// Delete a whole stroke by id: Local (id <= -2) from the client file; server ones via the
	// outbox (GM-locked -> local message, no RPC).
	protected void SM_DeleteStrokeById(int strokeId, notnull SCR_PlayerController pc)
	{
		// Local CHANNEL (not an optimistic server temp) -> client file.
		if (SM_MapDrawingStore.IsLocalId(strokeId) && !SM_DrawOutbox.IsServerTemp(strokeId))
		{
			SM_LocalDrawingPersistence.GetInstance().RemoveLocal(strokeId);
			return;
		}
		SM_MapDrawingData sdel = SM_DrawCanvas.GetStrokeData(strokeId);
		if (sdel && sdel.m_iGmLocked != 0 && !SM_IsEditorMap())
			pc.SM_ShowPlaceDenied(SM_EPlaceDenyReason.DRAW_LOCKED, 0);	// GM-locked — hands off (no optimistic removal)
		else
			SM_DrawOutbox.SubmitRemove(strokeId);	// batched/optimistic (a temp just gets cancelled)
	}

	// Пад-режим панелі: інструмент мапи щойно тогльнувся (напр. подвійний тап хрестовини =
	// компас/лінійка) — відкочуємо, щоб гортання нашого меню не вмикало ванільні інструменти.
	protected bool m_bSMToolGuardBusy;	// захист від повторного входу (SetActive сам фаєрить інвокер)
	protected void SM_OnToolToggledGuard(SCR_MapToolEntry entry)
	{
		if (!m_bSMPanelPadNav || !entry || m_bSMToolGuardBusy)
			return;
		m_bSMToolGuardBusy = true;
		entry.SetActive(false);
		m_bSMToolGuardBusy = false;
	}

	// Tool hotkeys (PC only). They go through the panel's own OnAction so the buttons light up and the
	// second press toggles the tool off, exactly as clicking them does.
	protected void SM_OnToolPencil(float value, EActionTrigger reason)
	{
		SM_HotkeyTool(SM_DrawPanel.ACT_PENCIL);
	}

	protected void SM_OnToolEraser(float value, EActionTrigger reason)
	{
		SM_HotkeyTool(SM_DrawPanel.ACT_ERASER);
	}

	protected void SM_OnToolFill(float value, EActionTrigger reason)
	{
		SM_HotkeyTool(SM_DrawPanel.ACT_FILL);
	}

	protected void SM_HotkeyTool(int act)
	{
		if (!m_bSMMapOpen || !m_DrawPanel || m_MarkerEditRoot)
			return;
		if (!m_bSMDrawPanelShown)
			return;	// панель прихована — інструментів нема чим показати, отже й вмикати нічого

		InputManager tim = GetGame().GetInputManager();
		if (!tim || !tim.IsUsingMouseAndKeyboard())
			return;

		m_DrawPanel.OnAction(act, 0);
	}

	protected void SM_OnTplApply(float value, EActionTrigger reason)
	{
		SM_TplButton(SM_DrawPanel.ACT_TPL_APPLY);
	}

	protected void SM_OnTplCancel(float value, EActionTrigger reason)
	{
		// Routed through the shared back handler: the default binding is B, the same button
		// AM_Cancel and MenuBack arrive on, and the debounce in there is what makes the three
		// listeners act once. A rebound key lands here too and behaves identically.
		SM_TryTemplateBack();
	}

	protected void SM_OnTplAdd(float value, EActionTrigger reason)
	{
		SM_TplButton(SM_DrawPanel.ACT_TPL_ADD);
	}

	protected void SM_OnTplRemove(float value, EActionTrigger reason)
	{
		SM_TplButton(SM_DrawPanel.ACT_TPL_REMOVE);
	}

	//! A template action from the map side: a remote click on the matching panel button. The panel
	//! decides whether the button could be clicked at all; here only the map-level context is vetted.
	protected void SM_TplButton(int act)
	{
		if (!m_bSMMapOpen || !m_DrawPanel || m_MarkerEditRoot)
			return;

		// Inside the panel the actions work too — the pad has no other way to press these buttons,
		// they are unfocusable by design. Except B: in the panel it stays "back" (SM_TryPanelBack
		// closes the list), and stepping the flow back on top of that would act twice on one press.
		if (m_bSMPanelPadNav)
		{
			if (act == SM_DrawPanel.ACT_TPL_CANCEL || !m_DrawPanel.IsTemplatesOpen())
				return;
		}

		// A key rebound onto a letter must not fire while the player is typing the name. The pad's Y
		// is still the Confirm — a controller has no other way to press it.
		InputManager im = GetGame().GetInputManager();
		if (im && im.IsUsingMouseAndKeyboard() && m_DrawPanel.IsTypingName())
			return;

		if (!m_DrawPanel.PressTemplateButton(act))
			return;

		// EAT the press that armed the flow. On the pad, A is BOTH this button and the map action, and
		// Apply clears the panel focus on the spot — so the same held A read as a fresh map click and
		// dropped a shape's first point under the cursor (screen centre) before the player could aim.
		//
		// Marked down UNCONDITIONALLY, not from GetActionValue: we are standing inside this button's
		// own listener, so the button IS down — asking the input manager only risked reading the other
		// action's value before it updated this frame, which is what let the press through. If the
		// action was rebound off A, the next poll sees the button up, fires a harmless release and
		// clears this by itself.
		if (im && !im.IsUsingMouseAndKeyboard())
		{
			m_bSMPadDrawDown     = true;
			m_bSMPadConfirmDown  = true;
			m_bSMPickedThisPress = true;	// and the release must not create a marker either
		}
	}

	//! Копія останньої мітки гравця в точку курсора. Клавіатура — AM_CopyLast, геймпад — Y
	//! (AM_CopyLastPad, опитується у своєму полері).
	//!
	//! Alt + the left button is only the DEFAULT BINDING (an InputSourceCombo in the conf), not
	//! something this code knows about: it listens to the action, whatever the player has moved it to.
	//! That is the whole point of the rewrite — the old version tested the Alt modifier by hand inside
	//! the left-button poll, so rebinding it anywhere else would have changed nothing.
	protected void SM_OnCopyLast(float value, EActionTrigger reason)
	{
		if (!m_bSMMapOpen || !SM_CanAcceptMapClick() || m_MarkerEditRoot)
			return;
		if (!SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
			return;
		if (m_iSMCarryId != -1 || m_bSMPointing)
			return;
		if (!m_SMLastTemplate || !SM_MarkerConfig.GetInstance().m_bAllowCopyLast)
			return;
		if (SM_IsEditorMap() && !SM_GmState.s_bMarkerView)
			return;

		InputManager cim = GetGame().GetInputManager();
		if (!cim || !cim.IsUsingMouseAndKeyboard())
			return;	// пад іде через AM_CopyLastPad у своєму полері

		SM_PlaceCopyAtCursor();
	}

	//! Подвійний клік: на мітці — редагувати, на пустому місці — створити.
	//!
	//! The engine detects the double click (InputFilterDoubleClick on AM_MarkerEdit), not a stopwatch
	//! of ours — so the threshold is the one shown in the keybind menu, and a player can move the
	//! action off the left button entirely. The pad never comes here: a tap of A does this directly.
	protected void SM_OnMarkerEdit(float value, EActionTrigger reason)
	{
		if (!m_bSMMapOpen || !SM_CanAcceptMapClick() || m_MarkerEditRoot)
			return;
		if (!SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
			return;
		if (m_iSMCarryId != -1 || m_bSMPointing)
			return;	// у руці мітка / вказуємо — другий клік належить їм

		InputManager eim = GetGame().GetInputManager();
		if (!eim || !eim.IsUsingMouseAndKeyboard())
			return;

		if (m_DrawCanvas && m_DrawCanvas.IsActive())
			return;	// інструмент малювання забирає ЛКМ повністю

		if (m_DrawPanel)
		{
			int ppx, ppy;
			SM_CursorPhysPx(ppx, ppy);
			if (m_DrawPanel.IsCursorOver(ppx, ppy))
				return;	// клік належить панелі
		}

		SM_MarkerVisual u = SM_FindMarkerUnderCursor();
		if (u && u.m_Data)
		{
			SM_OpenEdit(u.m_Data);
			return;
		}

		if (SM_IsEditorMap())
			return;	// зевс створює мітки з тулбара, а не кліком по мапі

		int wx, wy;
		if (SM_GetCursorWorld(wx, wy))
			SM_OpenCreate(wx, wy);
	}

	// Delete: видалити мітку під курсором.
	protected void SM_OnDelete(float value, EActionTrigger reason)
	{
		if (!m_bSMMapOpen || !SM_CanAcceptMapClick())
			return;

		// AM_Delete now carries the pad button too, and the pad already deletes from its own poll —
		// without this the one X press would delete twice.
		InputManager dim = GetGame().GetInputManager();
		if (!dim || !dim.IsUsingMouseAndKeyboard())
			return;

		SM_MarkerVisual vis = null;
		if (SM_HasFeature(AM_EMapFeature.MARKER_TOOLS))
			vis = SM_FindMarkerUnderCursor();
		if (!vis || !vis.m_Data)
		{
			// Мітки під курсором нема — може, там штрих малюнка? Del стирає його цілком
			// (права перевіряє сервер: власник/GM).
			if (m_DrawCanvas && m_DrawPanel)	// stroke erase needs the drawing-tools feature
			{
				WorkspaceWidget dws = GetGame().GetWorkspace();
				if (dws)
				{
					int pdwx, pdwy;
					bool pdHaveW = SM_GetCursorWorld(pdwx, pdwy);
					int pcpx, pcpy;
					SM_CursorPhysPx(pcpx, pcpy);
					int strokeId = m_DrawCanvas.FindStrokeAtScreen(pcpx, pcpy, pdwx, pdwy, pdHaveW);
					if (strokeId != -1)
					{
						SCR_PlayerController dpc = SM_LocalPC();
						if (dpc)
							SM_DeleteStrokeById(strokeId, dpc);	// Local or server (with the GM-lock check)
					}
				}
			}
			return;
		}

		if (SM_BlockedByLock(vis.m_Data))	// залочена зевсом — гравець не видаляє (повідомлення + звук)
			return;

		int delId = vis.m_Data.m_iId;
		if (SM_MapMarkerStore.IsLocalId(delId))
		{
			SM_LocalMarkerPersistence.GetInstance().RemoveLocal(delId);	// Local — erase from the client file
			return;
		}
		SCR_PlayerController pc = SM_LocalPC();
		if (pc)
			pc.SM_RequestRemove(delId);	// сервер видаляє по ID і синкає всім
	}

}
