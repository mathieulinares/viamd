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

UTEST_MAIN();
