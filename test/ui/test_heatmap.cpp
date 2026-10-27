#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>
#include <utest.h>

#include <limits>
#include <vector>

struct heatmap {
    ImGuiContext* gui = nullptr;
    ImPlotContext* plot = nullptr;
};

UTEST_F_SETUP(heatmap) {
    utest_fixture->gui = ImGui::CreateContext();
    utest_fixture->plot = ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(900, 700);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
}

UTEST_F_TEARDOWN(heatmap) {
    ImPlot::DestroyContext(utest_fixture->plot);
    ImGui::DestroyContext(utest_fixture->gui);
}

static int render_heatmap(const std::vector<float>& values, int rows, int cols,
                          ImPlotHeatmapFlags flags, const char* format) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(900, 700));
    ImGui::Begin("Heatmap regression", nullptr, ImGuiWindowFlags_NoSavedSettings);
    int vertices = -1;
    if (ImPlot::BeginPlot("Heatmap", ImVec2(800, 600), ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxesLimits(0, cols, 0, rows, ImPlotCond_Always);
        // Lock setup before counting vertices so axes are not counted as cells.
        ImPlot::PlotToPixels(0, 0);
        ImDrawList* draw = ImPlot::GetPlotDrawList();
        const int before = draw->VtxBuffer.Size;
        ImPlot::PlotHeatmap("values", values.data(), rows, cols, -2, 2, format,
            ImPlotPoint(0, 0), ImPlotPoint(cols, rows), flags);
        vertices = draw->VtxBuffer.Size - before;
        ImPlot::EndPlot();
    }
    ImGui::End();
    ImGui::Render();
    return vertices;
}

UTEST_F(heatmap, nonfinite_cells_are_transparent_in_both_layouts) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (ImPlotHeatmapFlags flags : {ImPlotHeatmapFlags_None, ImPlotHeatmapFlags_ColMajor}) {
        // Warm up window layout before measuring the draw list.
        render_heatmap({nan, 0, inf, -inf}, 2, 2, flags, nullptr);
        EXPECT_EQ(render_heatmap({nan, 0, inf, -inf}, 2, 2, flags, nullptr), 4);
        EXPECT_EQ(render_heatmap({nan, nan, inf, -inf}, 2, 2, flags, nullptr), 0);
        EXPECT_EQ(render_heatmap({-1, 0, 1, 2}, 2, 2, flags, nullptr), 16);
        EXPECT_EQ(render_heatmap({nan, nan, inf, -inf}, 2, 2, flags, "%.1f"), 0);
        EXPECT_GT(render_heatmap({nan, 0, inf, -inf}, 2, 2, flags, "%.1f"), 4);
    }
}

UTEST_F(heatmap, nucleosome_sized_sparse_matrix_does_not_crash) {
    std::vector<float> values(147 * 2501, std::numeric_limits<float>::quiet_NaN());
    values[0] = -1;
    values[2501] = 0;
    values.back() = 1;
    render_heatmap(values, 147, 2501, ImPlotHeatmapFlags_None, nullptr);
    EXPECT_EQ(render_heatmap(values, 147, 2501, ImPlotHeatmapFlags_None, nullptr), 12);
}

static ImPlotRect render_full_extent_fit(bool zoom, bool fit) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImVec2(900,700));
    ImGui::Begin("Full extent fit",nullptr,ImGuiWindowFlags_NoSavedSettings);
    if (fit) ImPlot::SetNextAxesToFit();
    ImPlotRect limits;
    if (ImPlot::BeginPlot("Visible heatmap",ImVec2(800,600),ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxesLimits(zoom ? 40 : -.5,zoom ? 60 : 99.5,
            zoom ? 2 : -.5,zoom ? 4 : 9.5,zoom ? ImPlotCond_Always : ImPlotCond_Once);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1,-.5,99.5);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1,-.5,9.5);
        limits=ImPlot::GetPlotLimits();
        if (ImPlot::FitThisFrame()) {
            ImPlot::FitPoint(ImPlotPoint(-.5,-.5));
            ImPlot::FitPoint(ImPlotPoint(99.5,9.5));
        }
        // Only a zoomed-in subset is rendered, as in the production display summary.
        const float data[]={0,1};
        ImPlot::PlotHeatmap("subset",data,1,2,-2,2,nullptr,ImPlotPoint(40,2),ImPlotPoint(60,4));
        limits=ImPlot::GetPlotLimits();
        ImPlot::EndPlot();
    }
    ImGui::End();
    ImGui::Render();
    return limits;
}

UTEST_F(heatmap, fitting_visible_subset_restores_full_trajectory_and_member_extent) {
    render_full_extent_fit(true,false);
    const auto zoomed=render_full_extent_fit(true,false);
    EXPECT_NEAR(zoomed.X.Min,40,1e-8);
    EXPECT_NEAR(zoomed.X.Max,60,1e-8);
    render_full_extent_fit(false,true);
    const auto fitted=render_full_extent_fit(false,false);
    EXPECT_NEAR(fitted.X.Min,-.5,1e-8);
    EXPECT_NEAR(fitted.X.Max,99.5,1e-8);
    EXPECT_NEAR(fitted.Y.Min,-.5,1e-8);
    EXPECT_NEAR(fitted.Y.Max,9.5,1e-8);
}

UTEST_F(heatmap, comparison_grid_renders_32_bin_overlays_with_one_shared_bottom_legend) {
    double centers[32],density[32];
    for (int b=0;b<32;++b) { centers[b]=b+.5; density[b]=1./32; }
    for (int frame=0;frame<6;++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Comparison dashboard",nullptr,ImGuiWindowFlags_NoSavedSettings);
        int rendered=0;
        const float grid_height=ImGui::GetContentRegionAvail().y-90;
        const float top=ImGui::GetCursorScreenPos().y;
        ImGui::BeginChild("Grid",ImVec2(0,grid_height));
        if (ImGui::BeginTable("Parameter grid",3,ImGuiTableFlags_SizingStretchSame)) {
            for (int parameter=0;parameter<6;++parameter) {
                ImGui::TableNextColumn();
                ImGui::PushID(parameter);
                ImPlot::SetNextAxesToFit();
                if (ImPlot::BeginPlot("Parameter",ImVec2(-1,250),ImPlotFlags_NoLegend)) {
                    ImPlot::SetupAxes("deg","Density");
                    for (int group=0;group<3;++group) {
                        const char* label=group==0 ? "Whole trajectory" : group==1 ? "Period" : "Missing CSV parameter";
                        const ImVec4 color=ImPlot::GetColormapColor(group);
                        const int style=frame/2;
                        if (group<2) {
                            if (style==0) {
                                ImPlot::SetNextFillStyle(color,.35f);
                                ImPlot::SetNextLineStyle(color,3);
                                ImPlot::PlotBars(label,centers,density,32,1);
                            } else {
                                if (style==2) {
                                    ImPlot::SetNextFillStyle(color,.35f);
                                    ImPlot::PlotShaded(label,centers,density,32,0.0);
                                }
                                ImPlot::SetNextLineStyle(color,3);
                                ImPlot::PlotLine(label,centers,density,32);
                            }
                        } else ImPlot::PlotDummy(label);
                    }
                    auto* plot=ImPlot::GetCurrentPlot();
                    ImPlot::EndPlot();
                    EXPECT_TRUE((plot->Flags&ImPlotFlags_NoLegend)!=0);
                    EXPECT_GT(plot->PlotRect.GetHeight(),100.f);
                    ++rendered;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        EXPECT_EQ(rendered,6);
        ImGui::EndChild();
        EXPECT_GE(ImGui::GetCursorScreenPos().y,top+grid_height);
        ImGui::BeginChild("Shared legend",ImVec2(0,0));
        for (int group=0;group<3;++group) {
            ImGui::PushID(group);
            bool visible=true;
            ImGui::Checkbox("Distribution",&visible);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
    }
}

UTEST_MAIN();
