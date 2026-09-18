# RidgesUmbilicsPlugin

基于 CGAL [Ridges_3](https://doc.cgal.org/latest/Ridges_3/index.html)（Cazals & Pouget 的脊线/脐点近似算法）的三角网格曲率特征分析功能插件，菜单入口在「功能 → 网格」分组（与网格布尔、网格质量、网格修复同组）。

## 功能

对目标三角网格 Component 逐顶点做 Monge jet 拟合（`CGAL::Monge_via_jet_fitting`，SVD 由 Eigen 提供），求得主曲率 k1/k2、主方向 d1/d2 与三阶（b0/b3）、四阶（P1/P2）系数，再经 `CGAL::Ridge_approximation` / `CGAL::Umbilic_approximation` 提取：

- **脊线（ridge lines）**：主曲率沿主方向取得极值的曲线，按类型分椭圆/双曲/crest 与最大/最小共 6 类；
- **脐点（umbilics）**：两主曲率相等的点（k1=k2），分椭圆/双曲/非-generic 三类（如椭球面上有 4 个椭圆脐点）。

结果按类型落成**新建几何组件**（折线边/点，命名 `Ridge_*` / `Umbilic_*`），不修改原网格；`addGeometryComponent` 结构操作随 `FeatureSystem::invoke` 边界即时发出通知并进入 undo 记录。

## 参数（下标与 `setup()` 注册顺序一一对应）

| 下标 | 类型 | 名称 | 说明 |
| ---- | ---- | ---- | ---- |
| 0 | Selector | 目标 Component | Component 类型单选 |
| 1 | Combo | 脊线类型 | 最大/最小/crest/全部（默认全部） |
| 2 | Combo | 脊线阶数 | 三阶（b0/b3 标注）/四阶（额外拟合 P1/P2，支持 crest 与 sharpness） |
| 3 | Int | 拟合邻域环数（0=自动） | 0 时按拟合阶数自动扩环至点数充足 |
| 4 | Combo | 是否计算脐点 | 是/否（默认是） |

## 输入约束

- 仅接受**纯三角表面网格**（含体单元或非三角面时拒绝执行）；
- 邻域点数不足拟合阶要求（`(d+1)(d+2)/2`）的顶点按平坦量退化处理，不影响其余顶点；
- **处处脐点的退化曲面**（完整球面最典型，`|k1-k2|` 中位数相对曲率量级低于 `2e-2`）不存在脊线、脐点处处都是，直接返回明确结论、不生成组件（此时继续提取只会得到噪声脊线，其中的退化线段还会被 OCC 边构建拒绝而中断分析）。

## 依赖

- `plugins/shared/cgal_support`（CGAL 6.2，GPLv3，仅限 plugins/ 内使用）；
- Eigen 3.4（MPL2，纯头文件），经 `PreCess-deps.py` 分发到 `<deps>/eigen-3.4.0`，缺失时本插件跳过构建。
