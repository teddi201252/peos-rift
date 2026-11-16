// GLFW Test: GLFW on PeOS (Rift Ascend port, phase C). Everything goes
// through GLFW and glad, as Rift Ascend's client does: a 4.5 core context,
// the GL functions loaded by glad with glfwGetProcAddress, the window's
// size, the keyboard, the characters, the mouse, the wheel, the cursor.
// What to see in the window (a test for the eye, on the MacBook):
//   - a triangle turning: frames come (glfwSwapInterval(1): 60 a second)
//   - a square under the mouse: white; left button orange, right cyan,
//     middle magenta; the wheel makes it larger or smaller
//   - R, G or B held: the background red, green or blue (glfwGetKey too)
//   - Space: the pointer hidden or shown (glfwSetInputMode)
//   - each character typed: one more notch at the top left (up to 40)
//   - a frame along the edge: the framebuffer's size (follows a resize)
//   - Esc: the app closes (glfwSetWindowShouldClose)
// Its log (stdout) says what GLFW reports.
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>

static double g_mx = -100.0, g_my = -100.0, g_size = 60.0;
static int g_buttons, g_chars, g_hidden, g_fbw, g_fbh;

static void on_error(int code, const char *text) { printf("glfwtest: GLFW error 0x%x: %s\n", code, text); }

static void on_key(GLFWwindow *w, int key, int scancode, int action, int mods) {
    const char *name = glfwGetKeyName(key, scancode);
    printf("glfwtest: key %d (%s) scancode %d %s, mods 0x%x\n", key, name ? name : "-", scancode,
           action == GLFW_PRESS ? "down" : action == GLFW_RELEASE ? "up" : "repeat", mods);
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(w, GLFW_TRUE);
    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        g_hidden = !g_hidden;
        glfwSetInputMode(w, GLFW_CURSOR, g_hidden ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
    }
}

static void on_char(GLFWwindow *w, unsigned cp) {
    (void)w;
    g_chars++;
    printf("glfwtest: char U+%04X\n", cp);
}

static void on_cursor(GLFWwindow *w, double x, double y) {
    (void)w;
    g_mx = x;
    g_my = y;
}

static void on_button(GLFWwindow *w, int button, int action, int mods) {
    (void)w;
    if (button < 3) g_buttons = action == GLFW_PRESS ? g_buttons | (1 << button) : g_buttons & ~(1 << button);
    printf("glfwtest: button %d %s, mods 0x%x\n", button, action == GLFW_PRESS ? "down" : "up", mods);
}

static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w;
    (void)dx;
    g_size *= dy > 0 ? 1.15 : 1.0 / 1.15;
    if (g_size < 10.0) g_size = 10.0;
    if (g_size > 400.0) g_size = 400.0;
}

static void on_fb(GLFWwindow *w, int width, int height) {
    (void)w;
    g_fbw = width;
    g_fbh = height;
    printf("glfwtest: framebuffer %dx%d\n", width, height);
}

static const char *k_vs =
    "#version 450 core\n"
    "layout(location = 0) in vec2 p;\n"
    "uniform vec4 rect;\n"   // x, y, w, h in pixels, from the top left
    "uniform vec2 fb;\n"
    "uniform float rot;\n"
    "void main() {\n"
    "  vec2 c = p - 0.5;\n"
    "  c = vec2(c.x * cos(rot) - c.y * sin(rot), c.x * sin(rot) + c.y * cos(rot));\n"
    "  vec2 px = rect.xy + rect.zw * 0.5 + c * rect.zw;\n"
    "  gl_Position = vec4(px.x / fb.x * 2.0 - 1.0, 1.0 - px.y / fb.y * 2.0, 0.0, 1.0);\n"
    "}\n";
static const char *k_fs =
    "#version 450 core\n"
    "uniform vec4 col;\n"
    "out vec4 o;\n"
    "void main() { o = col; }\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        printf("glfwtest: shader: %s\n", log);
    }
    return s;
}

static GLint u_rect, u_fb, u_rot, u_col;

static void shape(GLuint vao, GLenum mode, int n, double x, double y, double w, double h, float rot, float r, float g,
                  float b) {
    glBindVertexArray(vao);
    glUniform4f(u_rect, (float)x, (float)y, (float)w, (float)h);
    glUniform2f(u_fb, (float)(g_fbw ? g_fbw : 1), (float)(g_fbh ? g_fbh : 1));
    glUniform1f(u_rot, rot);
    glUniform4f(u_col, r, g, b, 1.0f);
    glDrawArrays(mode, 0, n);
}

int main(void) {
    glfwSetErrorCallback(on_error);
    if (!glfwInit()) return 1;
    printf("glfwtest: %s, platform 0x%x\n", glfwGetVersionString(), glfwGetPlatform());
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    GLFWwindow *win = glfwCreateWindow(800, 600, "GLFW Test", NULL, NULL);
    if (!win) {
        printf("glfwtest: no window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    int version = gladLoadGL(glfwGetProcAddress);
    printf("glfwtest: glad GL %d.%d, %s, %s\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version),
           (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    glfwSwapInterval(1);
    glfwSetKeyCallback(win, on_key);
    glfwSetCharCallback(win, on_char);
    glfwSetCursorPosCallback(win, on_cursor);
    glfwSetMouseButtonCallback(win, on_button);
    glfwSetScrollCallback(win, on_scroll);
    glfwSetFramebufferSizeCallback(win, on_fb);
    glfwGetFramebufferSize(win, &g_fbw, &g_fbh);
    int ww = 0, wh = 0;
    glfwGetWindowSize(win, &ww, &wh);
    printf("glfwtest: window %dx%d, framebuffer %dx%d\n", ww, wh, g_fbw, g_fbh);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, k_vs));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, k_fs));
    glLinkProgram(prog);
    glUseProgram(prog);
    u_rect = glGetUniformLocation(prog, "rect");
    u_fb = glGetUniformLocation(prog, "fb");
    u_rot = glGetUniformLocation(prog, "rot");
    u_col = glGetUniformLocation(prog, "col");

    // a unit square (strip) and a triangle, with the 4.5 functions (DSA)
    static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    static const float tri[] = {0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    GLuint vao[2], vbo[2];
    glCreateVertexArrays(2, vao);
    glCreateBuffers(2, vbo);
    glNamedBufferStorage(vbo[0], sizeof(quad), quad, 0);
    glNamedBufferStorage(vbo[1], sizeof(tri), tri, 0);
    for (int i = 0; i < 2; i++) {
        glVertexArrayVertexBuffer(vao[i], 0, vbo[i], 0, 2 * sizeof(float));
        glEnableVertexArrayAttrib(vao[i], 0);
        glVertexArrayAttribFormat(vao[i], 0, 2, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(vao[i], 0, 0);
    }

    double last = glfwGetTime(), t0 = last;
    int frames = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        double t = glfwGetTime();
        float bg[3] = {0.07f + 0.05f * (float)sin(t), 0.08f, 0.12f + 0.05f * (float)cos(t)};
        if (glfwGetKey(win, GLFW_KEY_R) == GLFW_PRESS) bg[0] = 0.7f, bg[1] = 0.1f, bg[2] = 0.1f;
        if (glfwGetKey(win, GLFW_KEY_G) == GLFW_PRESS) bg[0] = 0.1f, bg[1] = 0.6f, bg[2] = 0.2f;
        if (glfwGetKey(win, GLFW_KEY_B) == GLFW_PRESS) bg[0] = 0.1f, bg[1] = 0.2f, bg[2] = 0.7f;
        glViewport(0, 0, g_fbw, g_fbh);
        glClearColor(bg[0], bg[1], bg[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // the frame along the framebuffer's edge
        const double e = 6.0;
        shape(vao[0], GL_TRIANGLE_STRIP, 4, 0, 0, g_fbw, e, 0, 0.0f, 0.9f, 1.0f);
        shape(vao[0], GL_TRIANGLE_STRIP, 4, 0, g_fbh - e, g_fbw, e, 0, 0.0f, 0.9f, 1.0f);
        shape(vao[0], GL_TRIANGLE_STRIP, 4, 0, 0, e, g_fbh, 0, 0.0f, 0.9f, 1.0f);
        shape(vao[0], GL_TRIANGLE_STRIP, 4, g_fbw - e, 0, e, g_fbh, 0, 0.0f, 0.9f, 1.0f);
        // the turning triangle in the middle
        double s = (g_fbw < g_fbh ? g_fbw : g_fbh) * 0.4;
        shape(vao[1], GL_TRIANGLES, 3, g_fbw / 2.0 - s / 2, g_fbh / 2.0 - s / 2, s, s, (float)t, 0.9f, 0.85f, 0.3f);
        // a notch per character typed
        for (int i = 0; i < g_chars && i < 40; i++)
            shape(vao[0], GL_TRIANGLE_STRIP, 4, 16 + i * 10, 16, 6, 18, 0, 0.8f, 0.8f, 0.85f);
        // the square under the mouse
        float r = 1, g = 1, b = 1;
        if (g_buttons & 1) r = 1.0f, g = 0.55f, b = 0.1f;
        else if (g_buttons & 2) r = 0.1f, g = 0.9f, b = 1.0f;
        else if (g_buttons & 4) r = 1.0f, g = 0.2f, b = 0.9f;
        shape(vao[0], GL_TRIANGLE_STRIP, 4, g_mx - g_size / 2, g_my - g_size / 2, g_size, g_size, 0, r, g, b);

        glfwSwapBuffers(win);
        frames++;
        if (t - last >= 5.0) {
            printf("glfwtest: %.0f fps, %dx%d, %d characters\n", frames / (t - last), g_fbw, g_fbh, g_chars);
            frames = 0;
            last = t;
        }
    }
    printf("glfwtest: closed after %.1f s\n", glfwGetTime() - t0);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
