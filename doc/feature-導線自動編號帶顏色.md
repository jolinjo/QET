# 功能需求:導線「自動編號規則」附帶顏色(color follows conductor autonum rule)

**對象**:負責 QElectroTech Qt6 fork(分支 QT6-MCP)的開發 AI。
**提出方**:上游圖面自動化(qet-mcp)。**原則**:fork 改動越少越好、可向後相容。

---

## 1. 目標(User story)

導線自動編號已支援「多組規則、畫圖前於面板切換」(Settings › Display ›
Auto Numbering Selection)。現在希望**每一組 conductor 規則可綁一個顏色**,
使用者選了某組規則後,**新畫的導線同時套上該規則的線號與該規則的顏色**。

實務背景:公司線號依 IEC 60204-1 §2.2 分號段(AC控制 1XX、DC控制 2XX、PE…),
且每個線種依 §2.1 有**固定顏色**(AC控制紅、DC控制深藍、PE綠…)。目前線號能
自動、顏色卻要逐條手動改。把顏色綁進規則,選規則＝同時決定號段與線色,一步到位。

**不改變**:元件/圖頁規則不需要顏色;未設顏色的既有規則行為完全不變。

---

## 2. 現況(程式碼佐證)

- **規則資料模型**:`NumerotationContext`
  - `sources/autoNum/numerotationcontext.h` — 內部只有 `QStringList content_`
    (每筆 `type|value|increase|initialvalue`)。
  - `numerotationcontext.cpp`:`toXml()`(約 L162)寫出 `<part …>`;
    `fromXml()`(約 L184)只讀 `<part>`(**注意**:載入權威來源是 `<part>`,
    `formula` 屬性載入時被忽略)。
- **規則存放**:`QETProject::m_conductor_autonum` 是
  `QHash<QString /*title*/, NumerotationContext>`。
- **XML 讀**:`qetproject.cpp` `readProjectPropertiesXml()` L1686–1695
  逐一 `nc.fromXml(elmt)` 後 `insert(title, nc)`;容器讀 `current_autonum`。
- **XML 寫**:`qetproject.cpp` `writeDefaultPropertiesXml()` L1791–1803
  —— `conductor_autonum` 元素的 `title`/`formula` 屬性是**在這裡**用
  `setAttribute` 補上的(不是在 `NumerotationContext::toXml` 裡)。
- **套用時機(核心)**:`ConductorAutoNumerotation::newProperties()`
  `sources/conductorautonumerotation.cpp` L141–160 —— 新導線建立時,取當前
  作用中規則的 context,`cp.m_formula = formula;`(L154)。**這就是要加顏色的點。**
- **導線顏色欄位**:`ConductorProperties::color`(QColor)
  `sources/conductorproperties.h` L86;序列化見 `conductorproperties.cpp`
  L273–274(`e.setAttribute("color", color.name())`)。

---

## 3. 設計(建議最小改動)

### 3.1 資料模型:把顏色存進 NumerotationContext

在 `NumerotationContext` 加一個可選顏色成員(顏色只對 conductor 規則有意義,
element/folio 留空不影響):

- `numerotationcontext.h`:新增 `private: QString m_color;` 與
  `QString color() const;` / `void setColor(const QString&);`。
- `numerotationcontext.cpp`
  - `toXml(doc, tag)`:建立元素後,`if (!m_color.isEmpty())
    element.setAttribute("color", m_color);`
  - `fromXml(e)`:`m_color = e.attribute("color");`(缺屬性→空字串→無顏色)。

> 這樣顏色隨 `QHash<QString, NumerotationContext>` 一起流動,讀寫、複製、
> `next()` 全部自動帶著走,**qetproject 的讀寫幾乎不用動**(L1791–1803 的
> `toXml` 會把 color 一併寫出;L1686 的 `fromXml` 會一併讀入)。

### 3.2 套用時機:新導線同時套色(核心行為)

`conductorautonumerotation.cpp` `newProperties()`,在 L154 之後:

```cpp
cp.m_formula = formula;                 // 既有
const QString ruleColor = context.color();
if (!ruleColor.isEmpty()) {
    cp.color = QColor(ruleColor);       // ← 新增:規則顏色套到新導線
    // 若要單色線,確保不被 bicolor 蓋掉:
    // cp.m_bicolor = false;
}
```

- 只在「規則有設顏色」時覆寫,否則維持現行(繼承 diagram 預設導線色)。
- 這條路徑是「新電位的第一條導線」在賦號時走的;`numeratePotential()`
  (L166+)是加到既有電位時複製電位屬性,**不需**改(顏色會隨電位一致,
  符合「同電位同色」)。

### 3.3 UI:規則編輯器加顏色選擇

在自動編號規則管理介面加一個「顏色」控制項,只在編輯 **conductor** 分頁顯示:

- 檔案:`sources/autoNum/ui/autonumberingmanagementw.{h,cpp,ui}`
  (規則清單/編輯);必要時配合 `formulaautonumberingw` 顯示預覽。
- 控制項:一顆顯示目前色的按鈕,點擊開 `QColorDialog`;或提供 §2.1 六個
  預設色的下拉(黑/紅/深藍/淺藍/綠/橘)+ 自訂。
- 存規則時把選定色寫入該規則的 `NumerotationContext::setColor(...)`;
  載入規則時把 `color()` 還原到控制項。
- element/folio 規則:隱藏此控制項(或忽略)。

### 3.4 XML schema(向後相容)

```xml
<conductors_autonums current_autonum="1xx-AC控制線" freeze_new_conductors="false">
  <conductor_autonum title="1xx-AC控制線" formula="%sequ_1" color="#FF0000">
    <part increase="1" type="unit" value="101"/>
  </conductor_autonum>
  <conductor_autonum title="PE-接地線" formula="PE" color="#008000">
    <part increase="1" type="string" value="PE"/>
  </conductor_autonum>
</conductors_autonums>
```

- 舊檔沒有 `color` 屬性 → `m_color` 空 → 行為同現在。新欄位純附加。

---

## 4. 預期用法與顏色對照(§2.1 / §2.2)

規則名沿用我方已建立者;`color` 依 IEC 60204-1 §2.1:

| 規則 title | 號段 formula | 顏色(§2.1) | color |
|---|---|---|---|
| L1-動力線 / L2 / L3 | `%sequ_1L1`… | 動力主迴路 黑 | `#000000` |
| 1xx-AC控制線 | `%sequ_1`(101起) | AC 控制 紅 | `#FF0000` |
| 2xx-DC控制線 | `%sequ_1`(201起) | DC 控制 深藍 | `#00008B` |
| 0V-零電位線 | `0V%sequ_1` | DC 回路(建議同深藍,或內規另定) | `#00008B` |
| 4xx-類比信號線 | `%sequ_1`(401起) | (§2.1 未定,內規補) | — |
| PE-接地線 | `PE` | 保護接地 綠 | `#008000` |
| (中性線 N,若另建) | — | 淺藍 | `#ADD8E6` |

> 規則名採「代碼-線種」格式(代碼前置),配合 §8 的字母排序即可自然分組。

---

## 5. 相容性 / 邊界

- **不回溯**:改規則顏色**不**自動重上既有導線的色(與「autonum 不回溯重編號」
  一致)。若要,另做一個「將此規則顏色套到所有用此規則的導線」動作(可選)。
- **雙色線**:若導線 `m_bicolor=true`,顏色以既有雙色邏輯為主;規則色是否
  關閉雙色請保守處理(預設不動 bicolor,除非顏色明確覆寫,見 3.2 註解)。
- element/folio 規則不受影響。
- 匯出/CLI 渲染不需改(顏色本就是導線屬性)。

## 6. 驗收標準(建議測試)

1. 規則設 `color="#FF0000"` 且為 current → 新畫導線 `properties().color == #FF0000`
   且線號正確。
2. 規則未設 color → 新導線顏色維持 diagram 預設(行為不變)。
3. 存檔後 `<conductor_autonum … color="…">` 有寫出;重開專案 color 還原。
4. 舊專案(無 color 屬性)開啟不報錯、行為不變。
5. element/folio 規則不出現顏色欄、序列化不含 color。
6. 同電位追加導線 → 顏色與電位一致(走 `numeratePotential`)。

## 7. 版本 / commit(fork 慣例)

- fork 的 commit 需 **CMakeLists patch 版本 +1**、中文說明。
- 動到 `.ui` 需確認 `uic` 產生無誤;`autoNum/` 下改動請跑既有自動編號相關流程
  手動驗一次(建規則→設色→畫線→存檔重開)。
- 保持改動聚焦在:`numerotationcontext.*`、`conductorautonumerotation.cpp`、
  `autoNum/ui/autonumberingmanagementw.*`(＋必要的 `.ui`)。

---

## 附加需求(獨立小改):自動編號規則清單「字母排序」

**問題**:規則存於 `QHash<QString, NumerotationContext>`,各處填清單都直接用
`...AutoNum().keys()`,而 `QHash::keys()` 是**雜湊順序(等於隨機)**,導致選單
裡規則排列雜亂(例如動力 L1/L2/L3 被打散)。**改檔無法解決**(QET 載入後照
hash 重排)。

**需求**:填清單前把 keys **字母排序**(`std::sort` / `QStringList::sort()`)再
`addItems`。規則名已採「代碼-線種」前置格式(`0V-`/`1xx-`/`2xx-`/`4xx-`/`L1-`/
`L2-`/`L3-`/`PE-`),字母排序後即成合理號段順序、且同群(動力 Lx)相鄰。

**要改的填清單處(全部 conductor/element/folio 同樣處理)**:

- `sources/autoNum/ui/autonumberingdockwidget.cpp` L168、L245
  (`keys_conductor = m_project->conductorAutoNum().keys();` 之後 `.sort()`)。
- `sources/ui/diagrampropertiesdialog.cpp` L72、L146
  (`addItems(...conductorAutoNum().keys())` → 先取 keys、sort、再 addItems)。
- `sources/ui/configpage/projectconfigpages.cpp` L317、L321。
- `sources/ui/titleblockpropertieswidget.cpp` L482(folio)。
- `sources/autoNum/ui/selectautonumw.*`(自動編號選擇面板本體)—— 確認其
  來源清單同樣經排序。

> 若日後要**完全自訂順序**(非字母),再考慮於規則加 `order` 屬性並依它排;
> 目前「代碼前置＋字母排序」已足夠,優先採此最小改動。

**驗收**:同一組規則,選單顯示為字母序(`0V-…, 1xx-…, 2xx-…, 4xx-…, L1-…,
L2-…, L3-…, PE-…`);動力三相相鄰。存檔/重開不影響(排序純顯示層)。

---

## 附加需求(獨立小改):標題欄 LOGO「保持比例＋置中」

**問題**:標題欄 LOGO 目前**拉伸填滿格子、不保比例**。渲染在
`sources/titleblocktemplate.cpp` `renderCell()` 的 LogoCell 分支(約 L1684–1691):

```cpp
if (vector_logos_.contains(cell.logo_reference)) {
    vector_logos_[cell.logo_reference]->render(&painter, cell_rect);   // SVG 拉滿
} else if (bitmap_logos_.contains(cell.logo_reference)) {
    painter.drawPixmap(cell_rect, bitmap_logos_[cell.logo_reference]); // PNG 拉滿
}
```

`drawPixmap(rect, pixmap)` 與 `QSvgRenderer::render(painter, bounds)` 都把圖
**縮放填滿 bounds、忽略長寬比** → 橫式 LOGO(如 960×256=3.75:1)塞進 2.14:1
的格子會被拉高變形。

**需求**:LOGO 依原始長寬比縮放到「能放進格子的最大尺寸」,並在格子內**置中**
(維持比例,四周留白)。

**改法**(維持比例＋置中,bitmap 與 SVG 同套邏輯):

```cpp
if (vector_logos_.contains(cell.logo_reference)) {
    QSvgRenderer *svg = vector_logos_[cell.logo_reference];
    QSize sz = svg->defaultSize();
    sz.scale(cell_rect.size(), Qt::KeepAspectRatio);
    QRect target(QPoint(0,0), sz);
    target.moveCenter(cell_rect.center());
    svg->render(&painter, target);
} else if (bitmap_logos_.contains(cell.logo_reference)) {
    const QPixmap &px = bitmap_logos_[cell.logo_reference];
    QSize sz = px.size();
    sz.scale(cell_rect.size(), Qt::KeepAspectRatio);
    QRect target(QPoint(0,0), sz);
    target.moveCenter(cell_rect.center());
    painter.drawPixmap(target, px);
}
```

**向後相容取捨(二擇一,建議 A)**:

- **A. 無條件保持比例置中**(最少程式碼):logo 拉伸本就非預期行為,直接改成
  上式。已依舊行為「把 PNG 補白到格子比例」的既有圖框,改後 logo 只是多一點
  置中留白、**不會變形或破版**,可接受。
- **B. 以屬性選擇**(完全不動舊行為):`<logo>` 加 `keep_aspect="true"`
  (預設 false＝維持現行拉滿);讀進 `TitleBlockCell`,renderCell 依旗標決定。
  本公司封面圖框再把該 logo 設 `keep_aspect="true"`。改動較多。

> 同一段也要改 DXF 匯出路徑的 logo(若有)以求一致;`renderDxf` 內若有畫 logo
> 一併處理。純顯示層,不影響 XML 綱要(採 A 時)。

**驗收**:960×256 的 LOGO 放進 2.14:1 格子 → 維持 3.75:1、置中、上下留白、
不變形;方形 logo 放寬格子 → 左右留白置中。
