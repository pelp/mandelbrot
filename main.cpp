#include <stdlib.h>
#include <pthread.h>
#include <iostream>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <ctime>

#define HEIGHT      35
#define WIDTH       80
#define REC_DPTH    128
#define SCALE       " .-oO0"
#define SCALE_LEN   6
#define SC_WIDTH    200
#define SC_HEIGHT   200
#define EXPOSURE    4

#define MIN(x, y) (x < y) ? x : y
#define MAX(x, y) (x < y) ? y : x

using namespace std;

typedef struct rgb {
    double r, g, b;
} rgb;

rgb RGB_BLACK = {0, 0, 0};

typedef struct hsv {
    double h, s, v;
} hsv;

typedef struct gradient_t {
    int len;
    rgb *color;
} gradient_t;

typedef struct settings_t {
    int *buf;
    int width, height, screen_x, screen_y;
    double start_x, start_y, end_x, end_y;
    int depth;
    int tid;
} settings_t;

gradient_t gradient_gen(double *colors, int len)
{
    gradient_t grad;
    grad.len = len;
    grad.color = new rgb[len];
    for (int i = 0; i < len; i++)
    {
        grad.color[i].r = colors[3*i];
        grad.color[i].g = colors[3*i+1];
        grad.color[i].b = colors[3*i+2];
    }
    return grad;
}

double nova_grad[5*3] = {0, 0, 0,
                         0, 0, 1,
                         1, 0, 0,
                         1, 0, 1,
                         1, 1, 1};
gradient_t nova = gradient_gen(nova_grad, 5);

rgb gradient(gradient_t grad, double index)
{
    rgb out = grad.color[0];
    for (int i = 0; i < grad.len-1; i++)
    {
        if (index*(grad.len-1) > i+1) continue;
        rgb color1 = grad.color[i];
        rgb color2 = grad.color[i+1];
        double amount = index*grad.len - i;
        out.r = (color1.r*(1-amount) + color2.r*amount);
        out.g = (color1.g*(1-amount) + color2.g*amount);
        out.b = (color1.b*(1-amount) + color2.b*amount);
        break;
    }
    return out;
}

rgb hsv2rgb(hsv in)
{
    double      hh, p, q, t, ff;
    long        i;
    rgb         out;

    if(in.s <= 0.0) {       // < is bogus, just shuts up warnings
        out.r = in.v;
        out.g = in.v;
        out.b = in.v;
        return out;
    }
    hh = in.h;
    if(hh >= 360.0) hh = 0.0;
    hh /= 60.0;
    i = (long)hh;
    ff = hh - i;
    p = in.v * (1.0 - in.s);
    q = in.v * (1.0 - (in.s * ff));
    t = in.v * (1.0 - (in.s * (1.0 - ff)));

    switch(i) {
    case 0:
        out.r = in.v;
        out.g = t;
        out.b = p;
        break;
    case 1:
        out.r = q;
        out.g = in.v;
        out.b = p;
        break;
    case 2:
        out.r = p;
        out.g = in.v;
        out.b = t;
        break;

    case 3:
        out.r = p;
        out.g = q;
        out.b = in.v;
        break;
    case 4:
        out.r = t;
        out.g = p;
        out.b = in.v;
        break;
    case 5:
    default:
        out.r = in.v;
        out.g = p;
        out.b = q;
        break;
    }
    return out;     
}

double _diverge(double *zr, double *zi, double x, double y, int c, int depth)
{
    double tmp = (*zr)*(*zr) - (*zi)*(*zi) + x;
    *zi = y + 2*(*zr)*(*zi);
    *zr = tmp;
    double div = (*zi)*(*zi) + (*zr)*(*zr);
    if (div > 4 || c > depth) return c;
    return _diverge(zr, zi, x, y, c+1, depth);
}

double diverge(double x, double y, int depth)
{
    double zr = 0,
           zi = 0;
    return _diverge(&zr, &zi, x, y, 0, depth);
}

void generate_print(double center_x, double center_y, double r, int depth)
{
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            double div = diverge(center_x + (((double)x)/WIDTH - 0.5) * r,
                                 center_y + (((double)y)/HEIGHT - 0.5) * r,
                                 depth);
            printf("%c", SCALE[(int)((SCALE_LEN-1) * div/depth)]);
        }
        printf("\n");
    }
}

void *render_chunk(void *input)
{
    settings_t *settings = (settings_t *)input;
    // if (settings->tid == 0) 
    // {
    //     cout << "Depth: " << settings->depth << endl;
    //     cout << "Start X: " << settings->start_x << endl;
    //     cout << "Start Y: " << settings->start_y << endl;
    //     cout << "End X: " << settings->end_x << endl;
    //     cout << "End Y: " << settings->end_y << endl;
    // }
    for (int y = 0; y < settings->height; y++)
    {
        for (int x = 0; x < settings->width; x++)
        {
            double m_x = settings->start_x + x * (settings->end_x - settings->start_x) / settings->width;
            double m_y = settings->start_y + y * (settings->end_y - settings->start_y) / settings->height;
            settings->buf[y * settings->width + x] = diverge(m_x, m_y, settings->depth);
        }
    }
    pthread_exit(NULL);
}

void generate(SDL_Renderer *renderer, int width, int height, int depth, double center_x, double center_y, double r, double exposure)
{
    generate_print(center_x, center_y, r, depth);
    int n_threads = pow(2, ceil(log2(sqrt(width*height) / 64) / 2.0) * 2);
    int s_threads = sqrt(n_threads);
    pthread_t threads[n_threads];
    settings_t settings[n_threads];
    void *status;

    for (int c = 0; c < n_threads; c++)
    {
        settings[c].width = width / s_threads;
        settings[c].height = height / s_threads;
        settings[c].screen_x = (width * (c % s_threads)) / s_threads;
        settings[c].screen_y = (height * (c / (int)s_threads)) / s_threads;
        settings[c].depth = depth;
        settings[c].start_x = center_x + ((c % s_threads) / (double)s_threads - 0.5) * r;
        settings[c].start_y = center_y + ((c / (int)s_threads) / (double)s_threads - 0.5) * r;
        settings[c].end_x = settings[c].start_x + r/s_threads;
        settings[c].end_y = settings[c].start_y + r/s_threads;
        settings[c].buf = new int[settings[c].width * settings[c].height];
        settings[c].tid = c;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    for (int c = 0; c < n_threads; c++)
    {
        pthread_create(&threads[c], &attr, render_chunk, (void *)&settings[c]);
    }

    pthread_attr_destroy(&attr);

    for (int c = 0; c < n_threads; c++)
    {
        pthread_join(threads[c], &status);
        cout << "\rThread " << c << ": Done!";
    }
    cout << endl;
    for (int c = 0; c < n_threads; c++)
    {
        for (int y = 0; y < settings[c].height; y++)
        {
            for (int x = 0; x < settings[c].width; x++)
            {
                int div = settings[c].buf[x + y * settings[c].width];
                rgb out = (div > settings[c].depth-1) ? RGB_BLACK : gradient(nova, MIN(1, exposure*div/(double)settings[c].depth));

                SDL_SetRenderDrawColor(renderer, out.r*255, out.g*255, out.b*255, SDL_ALPHA_OPAQUE);
                SDL_RenderDrawPoint(renderer, settings[c].screen_x + x, settings[c].screen_y + y);
            }
        }
    }
}

int SDL_setup(int width, int height, SDL_Window **window, SDL_Renderer **renderer, SDL_Texture **texture)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        cout << "Failed to init SDL\n";
        return -1;
    }

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    
    *window = SDL_CreateWindow("Mandel",
                        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                        width, height,
                        SDL_WINDOW_HIDDEN);
    if (*window == NULL)
    {
        cout << "Window failed to create\n";
        return -1;
    }
    *renderer = SDL_CreateRenderer(*window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderTarget(*renderer, *texture);
    if (*renderer == NULL)
    {
        cout << "Failed to create renderer\n";
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int width = SC_WIDTH;
    int height = SC_HEIGHT;
    int depth = REC_DPTH;
    double exposure = EXPOSURE;
    if (argc < 4)
    {
        cout << "Requires x, y and r values:\n\t";
        cout << argv[0];
        cout << " <x> <y> <r>\n";
        return -1;
    }
    if (argc > 5)
    {
        width = pow(2, ceil(log2(stoi(argv[4]))));
        height = pow(2, ceil(log2(stoi(argv[5]))));
    }
    if (argc > 6)
    {
        depth = stoi(argv[6]);
    }
    if (argc > 7)
    {
        exposure = stod(argv[7]);
    }
    cout << "Starting mandel\n";
    cout << "Width: " << width << endl;
    cout << "Height: " << height << endl;
    cout << "Depth: " << depth << endl;
    SDL_Renderer *renderer = NULL;
    SDL_Window *window = NULL;
    SDL_Texture *texture = NULL;
    if (SDL_setup(width, height, &window, &renderer, &texture) < 0)
    {
        return -1;
    }
    cout << "Setup all windows\n";
    SDL_bool quit = SDL_FALSE;
    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    SDL_RenderClear(renderer);

    generate(renderer, width, height, depth, stod(argv[1]), stod(argv[2]), stod(argv[3]), exposure); 

    // SDL_RenderPresent(renderer);
    cout << "Rendered all\n";
    SDL_QueryTexture(texture, NULL, NULL, &width, &height);
    SDL_Surface* surface = SDL_CreateRGBSurface(0, width, height, 32, 0, 0, 0, 0);
    SDL_RenderReadPixels(renderer, NULL, surface->format->format, surface->pixels, surface->pitch);
    time_t t = time(0);
    tm *now = localtime(&t);
    string filename = "out.image.";
    filename += to_string(now->tm_year+1900);
    filename += "-";
    filename += to_string(now->tm_mon+1);
    filename += "-";
    filename += to_string(now->tm_mday);
    filename += " ";
    filename += to_string(now->tm_hour);
    filename += ":";
    filename += to_string(now->tm_min);
    filename += ":";
    filename += to_string(now->tm_sec);
    IMG_SavePNG(surface, filename.c_str());
    cout << "Saved image!" << endl;
    // while (!quit)
    // {
    //     SDL_Event event;
    //     
    //     while (SDL_PollEvent(&event))
    //     {
    //         switch(event.type)
    //         {
    //             case SDL_QUIT:
    //                 quit = SDL_TRUE;
    //                 break;
    //         }
    //     }
    //     SDL_Delay(10);
    // }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return 0;
}
