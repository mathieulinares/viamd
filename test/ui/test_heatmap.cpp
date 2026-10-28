#include <imgui.h>
#include <implot.h>
#include <implot_internal.h>
#include <components/lavery/lavery_plot.h>
#include <timeline_periods_ui.h>
#include <utest.h>

#include <limits>
#include <cmath>
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

struct PeriodView {
    uint32_t id;
    ImVec2 position;
    ImPlotRange range;
    ImVec2 lane;
    ImVec2 plot;
    float width=0;
    ImPlotRange plotted_range;
};

static void render_period_views(TimelinePeriods& periods, PeriodView& timeline, PeriodView& heatmap,
    const float* times, int frames, double& current) {
    ImGui::NewFrame();
    const auto input=ImPlot::GetInputMap();
    if (ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl || timeline_periods_dragging())
        ImPlot::GetInputMap().Pan=ImGuiMouseButton_Middle;
    for (auto* view : {&timeline,&heatmap}) {
        ImGui::PushID((int)view->id);
        ImGui::SetNextWindowPos(view->position);
        ImGui::SetNextWindowSize(ImVec2(430,300));
        ImGui::Begin(view==&timeline ? "Timeline periods" : "Heatmap periods",nullptr,ImGuiWindowFlags_NoSavedSettings);
        TimelinePeriodAxis axis;
        axis.view_id=view->id;
        axis.frame_times=times;
        axis.num_frames=frames;
        axis.view_beg=&view->range.Min;
        axis.view_end=&view->range.Max;
        axis.time_unit=times ? "ps" : "";
        view->lane=ImGui::GetCursorScreenPos();
        timeline_periods_lane(&periods,axis,&current);
        if (ImPlot::BeginPlot("Annotated plot",ImVec2(-1,-1),ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("Time","Member");
            ImPlot::SetupAxisLinks(ImAxis_X1,&view->range.Min,&view->range.Max);
            ImPlot::SetupAxisLimits(ImAxis_Y1,0,10,ImPlotCond_Always);
            timeline_periods_plot(&periods,axis,current);
            view->plot=ImPlot::GetPlotPos();
            view->width=ImPlot::GetPlotSize().x;
            view->plotted_range=ImPlot::GetPlotLimits().X;
            ImPlot::EndPlot();
        }
        ImGui::End();
        ImGui::PopID();
    }
    ImPlot::GetInputMap()=input;
    ImGui::Render();
}

static ImVec2 period_point(const PeriodView& view, double time) {
    return ImVec2(view.plot.x+(float)((time-view.range.Min)/view.range.Size())*view.width,
        view.lane.y+ImGui::GetFrameHeight()*.5f);
}

UTEST_F(heatmap, shared_period_lane_creates_marker_with_irregular_time_and_isolated_view_geometry) {
    TimelinePeriods periods;
    const float times[]={0,10,30,60,100};
    double current=0;
    PeriodView timeline{101,ImVec2(0,0),ImPlotRange(0,100)};
    PeriodView heatmap{102,ImVec2(450,0),ImPlotRange(20,80)};
    render_period_views(periods,timeline,heatmap,times,5,current);
    render_period_views(periods,timeline,heatmap,times,5,current);
    auto& io=ImGui::GetIO();
    io.MousePos=period_point(heatmap,30);
    io.AddKeyEvent(ImGuiMod_Shift,true);
    render_period_views(periods,timeline,heatmap,times,5,current);
    io.MouseDown[0]=true;
    render_period_views(periods,timeline,heatmap,times,5,current);
    io.MouseDown[0]=false;
    render_period_views(periods,timeline,heatmap,times,5,current);
    ASSERT_EQ(periods.count,1);
    EXPECT_TRUE(timeline_period_is_marker(periods.items[0]));
    EXPECT_EQ(periods.items[0].beg,2);
    EXPECT_EQ(current,0);
    EXPECT_NEAR(timeline.range.Min,0,1e-8);
    EXPECT_NEAR(heatmap.range.Min,20,1e-8);
}

UTEST_F(heatmap, shared_period_lane_shift_drag_creates_frame_index_period) {
    TimelinePeriods periods;
    double current=0;
    PeriodView timeline{201,ImVec2(0,0),ImPlotRange(0,100)};
    PeriodView heatmap{202,ImVec2(450,0),ImPlotRange(0,100)};
    const auto render=[&]() { render_period_views(periods,timeline,heatmap,nullptr,101,current); };
    render(); render();
    auto& io=ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Shift,true);
    io.MousePos=period_point(heatmap,20);
    render();
    io.MouseDown[0]=true;
    render();
    io.MousePos=period_point(heatmap,60);
    render();
    io.MouseDown[0]=false;
    render();
    ASSERT_EQ(periods.count,1);
    EXPECT_FALSE(timeline_period_is_marker(periods.items[0]));
    EXPECT_EQ(periods.items[0].beg,20);
    EXPECT_EQ(periods.items[0].end,60);
    EXPECT_NEAR(heatmap.range.Min,0,1e-8);
    EXPECT_NEAR(heatmap.range.Max,100,1e-8);
}

UTEST_F(heatmap, shared_period_lane_moves_resizes_and_scrubs_shared_annotations) {
    TimelinePeriods periods;
    auto* period=timeline_periods_add(&periods,"Shared",20,60,false);
    ASSERT_TRUE(period!=nullptr);
    double current=0;
    PeriodView timeline{301,ImVec2(0,0),ImPlotRange(0,100)};
    PeriodView heatmap{302,ImVec2(450,0),ImPlotRange(0,100)};
    const auto render=[&]() { render_period_views(periods,timeline,heatmap,nullptr,101,current); };
    render(); render();
    auto& io=ImGui::GetIO();
    io.MousePos=period_point(heatmap,40);
    render();
    io.MouseDown[0]=true;
    render();
    io.MousePos=period_point(heatmap,50);
    render();
    io.MouseDown[0]=false;
    render();
    EXPECT_EQ(period->beg,30);
    EXPECT_EQ(period->end,70);
    io.MousePos=period_point(heatmap,70);
    render();
    io.MouseDown[0]=true;
    render();
    io.MousePos=period_point(heatmap,80);
    render();
    io.MouseDown[0]=false;
    render();
    EXPECT_EQ(period->beg,30);
    EXPECT_EQ(period->end,80);
    io.AddKeyEvent(ImGuiMod_Ctrl,true);
    io.MousePos=period_point(heatmap,45);
    render();
    io.MouseDown[0]=true;
    render();
    io.MousePos=period_point(heatmap,55);
    render();
    io.MouseDown[0]=false;
    render();
    EXPECT_EQ(current,55);
    EXPECT_EQ(periods.count,1);
    io.AddKeyEvent(ImGuiMod_Ctrl,false);
    io.AddKeyEvent(ImGuiKey_Delete,true);
    render();
    EXPECT_EQ(periods.count,0);
}

UTEST_F(heatmap, annotation_lane_pan_and_zoom_link_only_to_its_own_plot) {
    TimelinePeriods periods;
    double current=0;
    PeriodView timeline{401,ImVec2(0,0),ImPlotRange(0,100)};
    PeriodView heatmap{402,ImVec2(450,0),ImPlotRange(0,100)};
    const auto render=[&]() { render_period_views(periods,timeline,heatmap,nullptr,101,current); };
    render(); render();
    auto& io=ImGui::GetIO();
    io.MousePos=period_point(heatmap,20);
    render();
    io.MouseDown[0]=true;
    render();
    io.MousePos=period_point(heatmap,30);
    render();
    io.MouseDown[0]=false;
    render();
    EXPECT_LT(heatmap.range.Min,0);
    EXPECT_NEAR(heatmap.range.Size(),100,1e-8);
    EXPECT_NEAR(heatmap.plotted_range.Min,heatmap.range.Min,1e-8);
    EXPECT_NEAR(heatmap.plotted_range.Max,heatmap.range.Max,1e-8);
    io.MouseWheel=1;
    render();
    EXPECT_NEAR(heatmap.range.Size(),90,.01);
    EXPECT_NEAR(heatmap.plotted_range.Min,heatmap.range.Min,1e-8);
    EXPECT_NEAR(heatmap.plotted_range.Max,heatmap.range.Max,1e-8);
    EXPECT_NEAR(timeline.range.Min,0,1e-8);
    EXPECT_NEAR(timeline.range.Max,100,1e-8);
}

UTEST_F(heatmap, shared_series_row_checkbox_hides_whole_trajectory_without_hiding_filtered_series) {
    bool whole=true,filtered=true;
    ImVec2 checkbox;
    for (int frame=0;frame<4;++frame) {
        auto& io=ImGui::GetIO();
        if (frame>=1) io.MousePos=checkbox;
        io.MouseDown[0]=frame==2;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(600,300));
        ImGui::Begin("Series controls",nullptr,ImGuiWindowFlags_NoSavedSettings);
        const auto start=ImGui::GetCursorScreenPos();
        checkbox=ImVec2(start.x+ImGui::GetFrameHeight()*.5f,start.y+ImGui::GetFrameHeight()*.5f);
        bool edit=false;
        ImGui::PushID("whole");
        lavery::draw_series_row("Whole trajectory",&whole,ImVec4(.25f,.7f,1,1),&edit);
        EXPECT_FALSE(edit);
        const auto name=ImGui::GetItemRectMin();
        EXPECT_GT(name.x,checkbox.x+ImGui::GetFrameHeight()*2);
        ImGui::PopID();
        ImGui::PushID("filtered");
        lavery::draw_series_row("Filtered series",&filtered,ImVec4(1,.5f,0,1),&edit);
        ImGui::PopID();
        ImGui::End();
        ImGui::Render();
    }
    EXPECT_FALSE(whole);
    EXPECT_TRUE(filtered);
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

UTEST_F(heatmap, sequence_profile_renders_two_partners_spread_and_optional_overlays) {
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const std::vector<double> x={1,2,nan,4,5};
    const std::vector<double> mean={40,40,nan,45,46}, low={40,39,nan,43,44}, high={40,41,nan,47,48};
    const std::vector<double> current={42,41,nan,44,45}, reference={0,0,nan,2,3};
    const std::vector<double> absent(5,nan), singleton={nan,nan,nan,2,nan};
    for (int frame=0;frame<3;++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Sequence profile regression",nullptr,ImGuiWindowFlags_NoSavedSettings);
        ImPlot::SetNextAxisToFit(ImAxis_Y1);
        if (ImPlot::BeginPlot("Profile",ImVec2(800,600))) {
            ImPlot::SetupAxes("BP position","deg");
            ImPlot::SetupAxisLimits(ImAxis_X1,.5,5.5,ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_South,ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal);
            for (int side=0;side<2;++side) {
                const char* label=side ? "Partner 2 mean" : "Partner 1 mean";
                const ImVec4 color=ImPlot::GetColormapColor(side);
                lavery::plot_sequence_trace(label,x,mean,&low,&high,color);
                lavery::plot_sequence_trace(side ? "Partner 2 current" : "Partner 1 current",x,side ? absent : current,
                    nullptr,nullptr,color,ImPlotMarker_Diamond,true);
                lavery::plot_sequence_trace(side ? "Partner 2 reference" : "Partner 1 reference",x,side ? singleton : reference,
                    nullptr,nullptr,color,ImPlotMarker_Square);
            }
            auto* plot=ImPlot::GetCurrentPlot();
            EXPECT_EQ(plot->Items.GetLegendCount(),6);
            ImPlot::EndPlot();
            if (frame) {
                EXPECT_LE(plot->Axes[ImAxis_Y1].Range.Min,0.0);
                EXPECT_GE(plot->Axes[ImAxis_Y1].Range.Max,48.0);
            }
        }
        ImGui::End();
        ImGui::Render();
        const auto* draw=ImGui::GetDrawData();
        for (int list=0;list<draw->CmdListsCount;++list)
            for (const auto& vertex : draw->CmdLists[list]->VtxBuffer) {
                EXPECT_TRUE(std::isfinite(vertex.pos.x));
                EXPECT_TRUE(std::isfinite(vertex.pos.y));
            }
    }
}

UTEST_F(heatmap, structural_panels_link_bp_axes_with_percent_and_geometric_units) {
    double xmin=.5,xmax=12.5;
    const std::vector<double> x={1.5,2.5,3.5},bend={20,35,40},rise={3.4,3.2,3.5},occupancy={0,50,100};
    for (int frame=0;frame<4;++frame) {
        if (frame==2) { xmin=1; xmax=4; }
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Structural profiles",nullptr,ImGuiWindowFlags_NoSavedSettings);
        for (int panel=0;panel<3;++panel) {
            ImGui::PushID(panel);
            if (ImPlot::BeginPlot(panel==0 ? "Bend" : panel==1 ? "Rise" : "Occupancy",ImVec2(800,200))) {
                ImPlot::SetupAxes("BP midpoint",panel==0 ? "deg" : panel==1 ? "Angstrom" : "%");
                ImPlot::SetupAxisLimits(ImAxis_X1,xmin,xmax,ImPlotCond_Once);
                ImPlot::SetupAxisLinks(ImAxis_X1,&xmin,&xmax);
                if (panel==2) ImPlot::SetupAxisLimits(ImAxis_Y1,-5,105,ImPlotCond_Always);
                lavery::plot_sequence_trace("Mean",x,panel==0 ? bend : panel==1 ? rise : occupancy,
                    nullptr,nullptr,ImVec4(.25f,.7f,1,1));
                if (panel==0) {
                    const double threshold=30;
                    ImPlot::PlotInfLines("Threshold",&threshold,1,ImPlotInfLinesFlags_Horizontal);
                }
                const auto limits=ImPlot::GetPlotLimits();
                if (frame>=2) {
                    EXPECT_NEAR(limits.X.Min,1,1e-8);
                    EXPECT_NEAR(limits.X.Max,4,1e-8);
                }
                if (panel==2) {
                    EXPECT_EQ(limits.Y.Min,-5.0);
                    EXPECT_EQ(limits.Y.Max,105.0);
                }
                ImPlot::EndPlot();
            }
            ImGui::PopID();
        }
        ImGui::End();
        ImGui::Render();
    }
}

UTEST_F(heatmap, sequence_landmark_drag_has_unique_ids_and_does_not_pan) {
    double position=3.5;
    ImVec2 handle;
    ImGuiID active=0;
    const auto render=[&]() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Landmark dragging",nullptr,ImGuiWindowFlags_NoSavedSettings);
        for (int panel=0;panel<2;++panel) {
            ImGui::PushID(panel);
            // Same-parity plot IDs exposed the old redundant CRC hash of plot.ID into plot.Items.ID (= ID-1).
            char title[32];
            for (int n=0;;++n) {
                snprintf(title,sizeof(title),"Profile %d",n);
                if (ImGui::GetID(title)&1u) break;
            }
            if (ImPlot::BeginPlot(title,ImVec2(800,280),ImPlotFlags_NoLegend)) {
                ImPlot::SetupAxesLimits(.5,12.5,-5,105,ImPlotCond_Once);
                double x=position;
                bool held=false;
                lavery::drag_sequence_landmark(0,&x,&held);
                if (held) { position=x; active=GImGui->ActiveId; }
                double other=9.5;
                bool other_held=false;
                lavery::drag_sequence_landmark(1,&other,&other_held);
                EXPECT_FALSE(other_held);
                lavery::plot_sequence_landmark(position,"Step 3-4",ImVec4(1,.35f,.65f,1));
                if (panel==0) handle=ImPlot::PlotToPixels(position,50);
                const auto limits=ImPlot::GetPlotLimits();
                EXPECT_EQ(limits.X.Min,.5);
                EXPECT_EQ(limits.X.Max,12.5);
                ImPlot::EndPlot();
            }
            ImGui::PopID();
        }
        EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);
        ImGui::End();
        ImGui::Render();
    };
    render(); render();
    auto& io=ImGui::GetIO();
    io.MousePos=handle;
    render(); render();
    io.MouseDown[0]=true;
    render();
    EXPECT_NE(active,0u);
    io.MousePos.x+=80;
    render(); render();
    EXPECT_GT(position,3.5);
    io.MouseDown[0]=false;
    render();
}

UTEST_F(heatmap, sequence_landmarks_keep_step_boundaries_across_profile_views_without_affecting_fit) {
    const double position=7.5;
    for (int view=0;view<6;++view) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Landmarks",nullptr,ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushID(view);
        if (ImPlot::BeginPlot("Profile",ImVec2(800,600))) {
            ImPlot::SetupAxes(view==5 ? "Sequence position" : nullptr,view==5 ? "%" : "deg",
                view==5 ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoTickLabels);
            ImPlot::SetupAxesLimits(.5,12.5,-5,105,ImPlotCond_Always);
            lavery::setup_sequence_ticks(.5,12.5);
            const auto before=ImPlot::GetPlotLimits();
            std::vector<ImVec4> labels;
            lavery::plot_sequence_landmark(position,"Step 7-8",ImVec4(1,.35f,.65f,1),&labels);
            lavery::plot_sequence_landmark(position+.01,"Step 8-9",ImVec4(1,.35f,.65f,1),&labels);
            ASSERT_EQ(labels.size(),(size_t)2);
            EXPECT_GT(labels[1].y,labels[0].w);
            const auto plot_pos=ImPlot::GetPlotPos();
            EXPECT_GT(labels[0].y,plot_pos.y);
            EXPECT_LT(labels[1].w,plot_pos.y+ImPlot::GetPlotSize().y);
            lavery::plot_sequence_landmark(1000,"Outside view",ImVec4(1,.35f,.65f,1));
            for (const auto& tick : ImPlot::GetCurrentPlot()->Axes[ImAxis_X1].Ticker.Ticks)
                EXPECT_EQ(tick.PlotPos,std::floor(tick.PlotPos));
            const auto after=ImPlot::GetPlotLimits();
            EXPECT_EQ(before.X.Min,after.X.Min);
            EXPECT_EQ(before.X.Max,after.X.Max);
            EXPECT_EQ(ImPlot::GetCurrentPlot()->Items.GetLegendCount(),0);
            const double restored=ImPlot::PixelsToPlot(ImPlot::PlotToPixels(position,0)).x;
            EXPECT_NEAR(restored,7.5,1e-5);
            ImPlot::EndPlot();
        }
        ImGui::PopID();
        ImGui::End();
        ImGui::Render();
        const auto* draw=ImGui::GetDrawData();
        for (int list=0;list<draw->CmdListsCount;++list)
            for (const auto& vertex : draw->CmdLists[list]->VtxBuffer) {
                EXPECT_TRUE(std::isfinite(vertex.pos.x));
                EXPECT_TRUE(std::isfinite(vertex.pos.y));
            }
    }
}

UTEST_F(heatmap, sequence_ticks_remain_integer_when_zoomed_between_base_pairs) {
    const double ranges[][2]={{.5,147.5},{53,54},{53.1,53.9}};
    for (int i=0;i<3;++i) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Integer sequence ticks",nullptr,ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushID(i);
        if (ImPlot::BeginPlot("Profile",ImVec2(800,600))) {
            ImPlot::SetupAxesLimits(ranges[i][0],ranges[i][1],0,1,ImPlotCond_Always);
            lavery::setup_sequence_ticks(ranges[i][0],ranges[i][1]);
            ImPlot::SetupFinish();
            const auto& axis=ImPlot::GetCurrentPlot()->Axes[ImAxis_X1];
            EXPECT_FALSE(axis.ShowDefaultTicks);
            if (i==2) EXPECT_EQ(axis.Ticker.Ticks.Size,0);
            else EXPECT_GT(axis.Ticker.Ticks.Size,0);
            for (const auto& tick : axis.Ticker.Ticks) {
                EXPECT_EQ(tick.PlotPos,std::floor(tick.PlotPos));
                const char* label=axis.Ticker.TextBuffer.Buf.Data+tick.TextOffset;
                for (;*label;++label) EXPECT_TRUE(*label>='0' && *label<='9');
            }
            ImPlot::EndPlot();
        }
        ImGui::PopID();
        ImGui::End();
        ImGui::Render();
    }
}

UTEST_F(heatmap, occupancy_bars_stack_to_100_keep_partners_separate_and_skip_missing_positions) {
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(900,700));
    ImGui::Begin("Occupancy bars",nullptr,ImGuiWindowFlags_NoSavedSettings);
    if (ImPlot::BeginPlot("States",ImVec2(800,600))) {
        ImPlot::SetupAxesLimits(.5,3.5,0,100,ImPlotCond_Always);
        ImPlot::SetupFinish();
        const auto* draw=ImPlot::GetPlotDrawList();
        const int first=draw->VtxBuffer.Size;
        const std::vector<const char*> labels={"BI","BII"};
        const std::vector<double> partner1={80,NAN,25,20,NAN,75};
        const std::vector<double> partner2={60,NAN,10,40,NAN,90};
        lavery::plot_sequence_state_bars(labels,partner1,3,.36,.8);
        lavery::plot_sequence_state_bars(labels,partner2,3,.36,1.2);
        EXPECT_EQ(ImPlot::GetCurrentPlot()->Items.GetLegendCount(),2);
        const float gap_min=ImPlot::PlotToPixels(1.6,0).x;
        const float gap_max=ImPlot::PlotToPixels(2.4,0).x;
        const float top=ImPlot::PlotToPixels(1,100).y;
        bool reached_top=false;
        for (int i=first;i<draw->VtxBuffer.Size;++i) {
            const auto v=draw->VtxBuffer[i].pos;
            EXPECT_TRUE(std::isfinite(v.x) && std::isfinite(v.y));
            EXPECT_TRUE(v.x<gap_min || v.x>gap_max);
            if (std::fabs(v.y-top)<.01f) reached_top=true;
        }
        EXPECT_TRUE(reached_top);
        const int before=draw->VtxBuffer.Size;
        lavery::plot_sequence_state_bars(labels,{NAN,NAN},1,.36,2);
        EXPECT_EQ(draw->VtxBuffer.Size,before);
        ImPlot::EndPlot();
    }
    ImGui::End();
    ImGui::Render();
}

UTEST_F(heatmap, multiple_filtered_profile_series_keep_gaps_and_separate_occupancy_lanes) {
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(900,700));
    ImGui::Begin("Multiple profiles",nullptr,ImGuiWindowFlags_NoSavedSettings);
    if (ImPlot::BeginPlot("Series",ImVec2(800,600))) {
        ImPlot::SetupAxesLimits(.5,3.5,0,100,ImPlotCond_Always);
        ImPlot::SetupFinish();
        const auto* draw=ImPlot::GetPlotDrawList();
        const int first=draw->VtxBuffer.Size;
        const std::vector<double> x={1,2,3},a={20,NAN,40},b={60,NAN,80};
        const std::vector<double> lo={15,NAN,35},hi={25,NAN,45};
        lavery::plot_sequence_trace("Early",x,a,&lo,&hi,ImVec4(1,0,0,1));
        lavery::plot_sequence_trace("Late",x,b,nullptr,nullptr,ImVec4(0,0,1,1));
        lavery::plot_sequence_state_bars({"Early / BI","Early / BII"},{80,NAN,25,20,NAN,75},3,.2,.76);
        lavery::plot_sequence_state_bars({"Late / BI","Late / BII"},{40,NAN,10,60,NAN,90},3,.2,1.24);
        EXPECT_EQ(ImPlot::GetCurrentPlot()->Items.GetLegendCount(),6);
        const float gap_min=ImPlot::PlotToPixels(1.5,0).x,gap_max=ImPlot::PlotToPixels(2.5,0).x;
        EXPECT_GT(draw->VtxBuffer.Size,first);
        bool left=false,right=false;
        const float centre=ImPlot::PlotToPixels(1,0).x;
        for (int i=first;i<draw->VtxBuffer.Size;++i) {
            const auto v=draw->VtxBuffer[i].pos;
            EXPECT_TRUE(std::isfinite(v.x) && std::isfinite(v.y));
            EXPECT_TRUE(v.x<gap_min || v.x>gap_max);
            if (v.x<centre-5) left=true;
            if (v.x>centre+5 && v.x<gap_min) right=true;
        }
        EXPECT_TRUE(left); EXPECT_TRUE(right);
        ImPlot::EndPlot();
    }
    ImGui::End();
    ImGui::Render();
}

UTEST_F(heatmap, bend_occupancy_bars_preserve_step_midpoints_and_skip_nonfinite_samples) {
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(900,700));
    ImGui::Begin("Bend occupancy bars",nullptr,ImGuiWindowFlags_NoSavedSettings);
    if (ImPlot::BeginPlot("Bends",ImVec2(800,600))) {
        ImPlot::SetupAxesLimits(.5,4.5,0,10,ImPlotCond_Always);
        ImPlot::SetupFinish();
        const auto* draw=ImPlot::GetPlotDrawList();
        const int first=draw->VtxBuffer.Size;
        lavery::plot_sequence_bars("Occupancy",{1.5,2.5,NAN,3.5},{8,NAN,5,0},.7,ImVec4(1,.5f,0,1));
        EXPECT_GT(draw->VtxBuffer.Size,first);
        const float expected_left=ImPlot::PlotToPixels(1.15,0).x;
        const float expected_right=ImPlot::PlotToPixels(1.85,0).x;
        for (int i=first;i<draw->VtxBuffer.Size;++i) {
            const auto v=draw->VtxBuffer[i].pos;
            EXPECT_TRUE(std::isfinite(v.x) && std::isfinite(v.y));
            EXPECT_GE(v.x,expected_left-2);
            EXPECT_LE(v.x,expected_right+2);
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
    ImGui::Render();
}

UTEST_F(heatmap, time_heatmap_uses_nonuniform_cell_widths_and_matching_member_rows) {
    for (int frame=0;frame<2;++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Physical-time heatmap",nullptr,ImGuiWindowFlags_NoSavedSettings);
        if (ImPlot::BeginPlot("Time x sequence",ImVec2(800,600),ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxesLimits(5,125,-.5,1.5,ImPlotCond_Always);
            ImPlot::SetupFinish();
            const auto* draw=ImPlot::GetPlotDrawList();
            const int first=draw->VtxBuffer.Size;
            // Only member zero has valid data, and only the 10/50 ps samples are finite.
            lavery::plot_sequence_heatmap("samples",{1,NAN,3,NAN,NAN,NAN,NAN,NAN},2,{5,15,35,75,125},0,3);
            ASSERT_EQ(draw->VtxBuffer.Size-first,8);
            for (int v=first;v<draw->VtxBuffer.Size;++v) {
                const auto pixel=draw->VtxBuffer[v].pos;
                const auto point=ImPlot::PixelsToPlot(pixel);
                EXPECT_TRUE(std::isfinite(pixel.x) && std::isfinite(pixel.y));
                EXPECT_TRUE(std::fabs(point.x-5)<1e-4 || std::fabs(point.x-15)<1e-4 ||
                    std::fabs(point.x-35)<1e-4 || std::fabs(point.x-75)<1e-4);
                EXPECT_NEAR(std::fabs(point.y),.5,1e-5);
            }
            const auto limits=ImPlot::GetPlotLimits();
            const int legend_items=ImPlot::GetCurrentPlot()->Items.GetLegendCount();
            std::vector<ImVec4> labels;
            lavery::plot_heatmap_landmark(.5,"Step 1-2",ImVec4(1,.35f,.65f,1),labels);
            lavery::plot_heatmap_landmark(.51,"Nearby guide",ImVec4(1,.35f,.65f,1),labels);
            ASSERT_EQ(labels.size(),(size_t)2);
            EXPECT_GT(labels[1].y,labels[0].w);
            EXPECT_EQ(ImPlot::GetPlotLimits().Y.Min,limits.Y.Min);
            EXPECT_EQ(ImPlot::GetPlotLimits().Y.Max,limits.Y.Max);
            EXPECT_EQ(ImPlot::GetCurrentPlot()->Items.GetLegendCount(),legend_items);
            ImPlot::EndPlot();
        }
        ImGui::End();
        ImGui::Render();
    }
}

UTEST_F(heatmap, timeline_markers_and_periods_share_time_coordinates_without_changing_plot_scope) {
    for (int frame=0;frame<2;++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Timeline guides",nullptr,ImGuiWindowFlags_NoSavedSettings);
        if (ImPlot::BeginPlot("Time x sequence",ImVec2(800,600),ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxesLimits(10,100,-.5,10.5,ImPlotCond_Always);
            ImPlot::SetupFinish();
            auto* draw=ImPlot::GetPlotDrawList();
            const int first=draw->VtxBuffer.Size;
            const auto limits=ImPlot::GetPlotLimits();
            const int legend=ImPlot::GetCurrentPlot()->Items.GetLegendCount();
            std::vector<ImVec4> labels;
            const ImVec4 color(.2f,.7f,1,1);
            const int line_first=draw->VtxBuffer.Size;
            lavery::plot_sequence_landmark(30,"",color);
            const float expected_x=ImPlot::PlotToPixels(30,0).x;
            float left=INFINITY,right=-INFINITY;
            for (int v=line_first;v<draw->VtxBuffer.Size;++v) {
                left=std::min(left,draw->VtxBuffer[v].pos.x);
                right=std::max(right,draw->VtxBuffer[v].pos.x);
            }
            EXPECT_NEAR((left+right)*.5f,expected_x+.5f,.01f);
            EXPECT_GE(right-left,3.0f);
            const int horizontal_first=draw->VtxBuffer.Size;
            lavery::plot_heatmap_landmark(3,"",color,labels);
            float top=INFINITY,bottom=-INFINITY;
            for (int v=horizontal_first;v<draw->VtxBuffer.Size;++v) {
                if (draw->VtxBuffer[v].pos.x>ImPlot::GetPlotPos().x+10) continue;
                top=std::min(top,draw->VtxBuffer[v].pos.y);
                bottom=std::max(bottom,draw->VtxBuffer[v].pos.y);
            }
            EXPECT_NEAR((top+bottom)*.5f,ImPlot::PlotToPixels(10,3).y+.5f,.01f);
            EXPECT_GE(bottom-top,3.0f);
            labels.clear();
            lavery::plot_timeline_guide(30,30,"Marker",color,labels);
            lavery::plot_timeline_guide(31,80,"Period",color,labels);
            lavery::plot_timeline_guide(0,20,"Clipped period",color,labels);
            const size_t visible=labels.size();
            lavery::plot_timeline_guide(110,120,"Outside",color,labels);
            EXPECT_EQ(labels.size(),visible);
            EXPECT_EQ(visible,(size_t)3);
            for (size_t a=0;a<labels.size();++a) for (size_t b=a+1;b<labels.size();++b)
                EXPECT_TRUE(labels[a].z<=labels[b].x || labels[b].z<=labels[a].x ||
                    labels[a].w<=labels[b].y || labels[b].w<=labels[a].y);
            for (int v=first;v<draw->VtxBuffer.Size;++v)
                EXPECT_TRUE(std::isfinite(draw->VtxBuffer[v].pos.x) && std::isfinite(draw->VtxBuffer[v].pos.y));
            EXPECT_EQ(ImPlot::GetPlotLimits().X.Min,limits.X.Min);
            EXPECT_EQ(ImPlot::GetPlotLimits().X.Max,limits.X.Max);
            EXPECT_EQ(ImPlot::GetCurrentPlot()->Items.GetLegendCount(),legend);
            ImPlot::EndPlot();
        }
        ImGui::End();
        ImGui::Render();
    }
}

UTEST_F(heatmap, helix_wide_reference_is_a_marker_free_line_with_disconnected_bands) {
    for (int frame=0;frame<2;++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(900,700));
        ImGui::Begin("Helix-wide reference",nullptr,ImGuiWindowFlags_NoSavedSettings);
        if (ImPlot::BeginPlot("Reference",ImVec2(800,600),ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxesLimits(0,6,0,10,ImPlotCond_Always);
            ImPlot::SetupFinish();
            const auto* draw=ImPlot::GetPlotDrawList();
            const std::vector<double> x={1,2,NAN,4,5},y={3,3,NAN,7,7};
            const std::vector<double> lo={2,2,NAN,6,6},hi={4,4,NAN,8,8};
            const int band_begin=draw->VtxBuffer.Size;
            lavery::plot_sequence_trace("Reference band",x,y,&lo,&hi,ImVec4(0,1,0,1),ImPlotMarker_None);
            EXPECT_GT(draw->VtxBuffer.Size,band_begin);
            const float gap_min=ImPlot::PlotToPixels(2.1,0).x,gap_max=ImPlot::PlotToPixels(3.9,0).x;
            for (int i=band_begin;i<draw->VtxBuffer.Size;++i) {
                const auto p=draw->VtxBuffer[i].pos;
                EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y));
                EXPECT_TRUE(p.x<gap_min || p.x>gap_max);
            }
            const int line_begin=draw->VtxBuffer.Size;
            lavery::plot_sequence_trace("Reference line",x,y,nullptr,nullptr,ImVec4(0,1,0,1),ImPlotMarker_None);
            EXPECT_GT(draw->VtxBuffer.Size,line_begin);
            const float y1=ImPlot::PlotToPixels(1,3).y,y2=ImPlot::PlotToPixels(4,7).y;
            for (int i=line_begin;i<draw->VtxBuffer.Size;++i) {
                const auto p=draw->VtxBuffer[i].pos;
                EXPECT_TRUE(std::fabs(p.y-y1)<=2.5f || std::fabs(p.y-y2)<=2.5f);
            }
            ImPlot::EndPlot();
        }
        ImGui::End();
        ImGui::Render();
    }
}

UTEST_MAIN();
