#include <gfx/immediate_draw_utils.cpp>
#include <utest.h>

UTEST(immediate_geometry, indexed_spheres_preserve_triangles_normals_colors_and_picking) {
    auto* queue = immediate::queue_create("Sphere regression");
    const vec3_t center = {2,3,4};
    constexpr float radius = 2.5f;
    constexpr uint32_t color = 0x80553311, pick = 73;
    for (const auto& dimensions : {std::pair{2,3},std::pair{8,12},std::pair{12,8},std::pair{10,20}}) {
        immediate::queue_reset(queue);
        const auto [stacks,slices] = dimensions;
        immediate::sphere(queue,center,radius,color,pick,stacks,slices);
        EXPECT_EQ(md_array_size(queue->vertices),(size_t)(stacks+1)*(slices+1));
        EXPECT_EQ(md_array_size(queue->indices),(size_t)stacks*slices*6);
        EXPECT_LT(md_array_size(queue->vertices),(size_t)stacks*slices*4);
        size_t index = 0;
        const int corners[][2] = {{0,0},{1,1},{1,0},{0,0},{0,1},{1,1}};
        for (int i=0;i<stacks;++i) for (int j=0;j<slices;++j) {
            for (const auto& corner : corners) {
                const float phi = 3.14159265358979323846f*((float)(i+corner[0])/stacks)-3.14159265358979323846f*.5f;
                const float theta = (2*3.14159265358979323846f)*((float)(j+corner[1])/slices);
                const vec3_t normal = {cosf(phi)*cosf(theta),cosf(phi)*sinf(theta),sinf(phi)};
                ASSERT_LT(queue->indices[index],md_array_size(queue->vertices));
                const auto& vertex = queue->vertices[queue->indices[index++]];
                EXPECT_NEAR(vec3_distance(vertex.normal,normal),0,1e-6);
                EXPECT_NEAR(vec3_distance(vertex.coord,vec3_add(center,vec3_mul1(normal,radius))),0,1e-6);
                EXPECT_EQ(vertex.color,color);
                EXPECT_EQ(vertex.picking_idx,pick);
            }
        }
        immediate::sphere(queue,center,radius,color,pick,stacks,slices);
        EXPECT_EQ(md_array_size(queue->commands),(size_t)1);
        EXPECT_EQ(queue->commands[0].count,(uint32_t)(stacks*slices*12));
        for (size_t n=0;n<md_array_size(queue->indices);++n)
            EXPECT_LT(queue->indices[n],md_array_size(queue->vertices));
    }
    immediate::queue_destroy(queue);
}

UTEST(immediate_geometry, indexed_cylinders_preserve_order_caps_normals_and_picking) {
    auto* queue = immediate::queue_create("Cylinder regression");
    const vec3_t from = {2,3,4};
    constexpr uint32_t color = 0x80553311, pick = 73;
    for (int segments : {3,6,10,12,16,24}) for (const vec3_t to : {
        vec3_t{5,7,9},vec3_t{-2,3,4},vec3_t{2,3,7},vec3_t{2,-5,4}}) {
        immediate::queue_reset(queue);
        immediate::sphere(queue,from,.5f,color,pick,8,12);
        const size_t vertex_base = md_array_size(queue->vertices);
        const size_t index_base = md_array_size(queue->indices);
        for (int copy=0;copy<2;++copy) {
            const float radius = copy ? 1.25f : .08f;
            const uint32_t tint = color+copy, picking = pick+copy;
            immediate::cylinder(queue,from,to,radius,tint,picking,segments);
            const vec3_t axis = vec3_normalize(vec3_sub(to,from));
            const vec3_t up = fabsf(axis.x)<.9f ? vec3_t{1,0,0} : vec3_t{0,1,0};
            const vec3_t u = vec3_normalize(vec3_cross(up,axis)), v = vec3_cross(axis,u);
            size_t index = index_base+(size_t)copy*segments*12;
            for (int i=0;i<segments;++i) {
                const float th0 = 6.28318530717958647692f*((float)i/segments);
                const float th1 = 6.28318530717958647692f*((float)(i+1)/segments);
                const vec3_t n0 = vec3_add(vec3_mul1(u,cosf(th0)),vec3_mul1(v,sinf(th0)));
                const vec3_t n1 = vec3_add(vec3_mul1(u,cosf(th1)),vec3_mul1(v,sinf(th1)));
                const vec3_t a0 = vec3_add(from,vec3_mul1(n0,radius));
                const vec3_t a1 = vec3_add(from,vec3_mul1(n1,radius));
                const vec3_t b0 = vec3_add(to,vec3_mul1(n0,radius));
                const vec3_t b1 = vec3_add(to,vec3_mul1(n1,radius));
                const vec3_t coords[] = {a0,b1,b0,a0,a1,b1,from,a1,a0,to,b0,b1};
                const vec3_t neg_axis = vec3_mul1(axis,-1);
                const vec3_t normals[] = {n0,n1,n0,n0,n1,n1,neg_axis,neg_axis,neg_axis,axis,axis,axis};
                for (int corner=0;corner<12;++corner) {
                    ASSERT_LT(queue->indices[index],md_array_size(queue->vertices));
                    const auto& vertex = queue->vertices[queue->indices[index++]];
                    EXPECT_NEAR(vec3_distance(vertex.coord,coords[corner]),0,1e-6);
                    EXPECT_NEAR(vec3_distance(vertex.normal,normals[corner]),0,1e-6);
                    EXPECT_EQ(vertex.color,tint);
                    EXPECT_EQ(vertex.picking_idx,picking);
                }
            }
        }
        EXPECT_EQ(md_array_size(queue->vertices)-vertex_base,2*((size_t)(segments+1)*4+2));
        EXPECT_LT(md_array_size(queue->vertices)-vertex_base,(size_t)segments*20);
        EXPECT_EQ(md_array_size(queue->indices)-index_base,(size_t)segments*24);
        EXPECT_EQ(md_array_size(queue->commands),(size_t)1);
        EXPECT_EQ(queue->commands[0].count,(uint32_t)md_array_size(queue->indices));
    }
    immediate::queue_destroy(queue);
}

UTEST(immediate_geometry, cached_spheres_apply_new_transforms_colors_and_picking_after_reset) {
    auto* queue = immediate::queue_create("Sphere cache regression");
    for (int pass=0;pass<3;++pass) for (const auto& dimensions : {
        std::pair{8,12},std::pair{10,20},std::pair{12,8}}) {
        immediate::queue_reset(queue);
        const auto [stacks,slices] = dimensions;
        const vec3_t center = {(float)pass,2.0f*pass,-3.0f*pass};
        const float radius = pass+.25f;
        const uint32_t color = 0x80443322u+pass, pick = 100+pass;
        immediate::sphere(queue,center,radius,color,pick,stacks,slices);
        for (int i=0;i<=stacks;++i) for (int j=0;j<=slices;++j) {
            const float phi = 3.14159265358979323846f*((float)i/stacks)-3.14159265358979323846f*.5f;
            const float theta = (2*3.14159265358979323846f)*((float)j/slices);
            const vec3_t normal = {cosf(phi)*cosf(theta),cosf(phi)*sinf(theta),sinf(phi)};
            const auto& vertex = queue->vertices[i*(slices+1)+j];
            EXPECT_NEAR(vec3_distance(vertex.coord,vec3_add(center,vec3_mul1(normal,radius))),0,1e-6);
            EXPECT_NEAR(vec3_distance(vertex.normal,normal),0,1e-6);
            EXPECT_EQ(vertex.color,color);
            EXPECT_EQ(vertex.picking_idx,pick);
        }
    }
    EXPECT_EQ(md_array_size(queue->spheres),(size_t)3);
    immediate::queue_destroy(queue);
}

UTEST_MAIN();
