#include <stdlib.h>
#include <notcurses/notcurses.h>

static const uint32_t LOWCOLOR = 0x004080;
static const uint32_t HICOLOR = 0xddffdd;
static const uint32_t NANOSEC = 1000000000ull / 60; // 60 cps
#define MARGIN 2

static struct notcurses*
init(void){
  struct notcurses_options opts = {
    //.loglevel = NCLOGLEVEL_DEBUG,
    .margin_t = MARGIN / 2,
    .margin_r = MARGIN,
    .margin_b = MARGIN / 2,
    .margin_l = MARGIN,
    .flags = NCOPTION_NO_ALTERNATE_SCREEN |
             NCOPTION_SUPPRESS_BANNERS,
  };
  struct notcurses* nc = notcurses_init(&opts, stdout);
  return nc;
}

static int
colorize(struct ncplane* n){
  int y, x;
  ncplane_cursor_yx(n, &y, &x);
  uint32_t c = HICOLOR;
  ncplane_set_bg_rgb(n, 0x222222);
  while(y >= 0){
    while(x >= 0){
      uint16_t stylemask;
      uint64_t channels;
      char* egc = ncplane_at_yx(n, y, x, &stylemask, &channels);
      if(egc == NULL){
        return -1;
      }
      if(strcmp(egc, "") == 0){
        free(egc);
        --x;
        continue;
      }
      ncplane_set_fg_rgb(n, c);
      if(ncplane_putegc_yx(n, y, x, egc, NULL) < 0){
        free(egc);
        return -1;
      }
      if(c != LOWCOLOR){
        unsigned g = (c & 0xff00) >> 8u;
        --g;
        // only the leading character gets the bright almost-white
        c = (c & 0x000080) | (g << 8u);
      }
      free(egc);
      --x;
    }
    --y;
    x = ncplane_dim_x(n) - 1;
  }
  return 0;
}

static int
textplay(struct notcurses* nc, struct ncplane* tplane, struct ncvisual* ncv){
  char* buf = NULL;
  size_t buflen = 1;
  struct ncplane* stdn = notcurses_stdplane(nc);
  ncplane_set_scrolling(tplane, true);
  struct ncvisual_options vopts = {
    .n = ncplane_dup(stdn, NULL),
    .scaling = NCSCALE_STRETCH,
    .blitter = NCBLIT_PIXEL,
  };
  ncplane_move_below(vopts.n, tplane);
  wint_t wc;
  while((wc = getwc(stdin)) != WEOF){
    if(ncv){
      if(ncvisual_render(nc, ncv, &vopts) == NULL){
        return -1;
      }
    }
    ncplane_erase(tplane);
    char conv[7];
    mbstate_t mbs = {};
    size_t w = wcrtomb(conv, wc, &mbs);
    if(w == (size_t)-1){
      goto err;
    }
    conv[w] = '\0';
    char* tmp = realloc(buf, buflen + w);
    if(tmp == NULL){
      goto err;
    }
    buf = tmp;
    strcpy(buf + buflen - 1, conv);
    buflen += w;
    int pt = ncplane_puttext(tplane, 0, NCALIGN_LEFT, buf, NULL);
    if(pt < 0){
      goto err;
    }
    if(colorize(tplane)){
      goto err;
    }
    if(notcurses_render(nc)){
      goto err;
    }
    struct timespec ts = {
      .tv_sec = 0, .tv_nsec = NANOSEC,
    };
    if(!ncv){
      clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    }else{
      for(int i = 0 ; i < 2 ; ++i){
        ncvisual_decode(ncv);
      }
    }
  }
  ncplane_destroy(vopts.n);
  return 0;

err:
  ncplane_destroy(vopts.n);
  free(buf);
  return -1;
}

static struct ncplane*
textplane(struct notcurses* nc){
  int dimy, dimx;
  struct ncplane* stdn = notcurses_stddim_yx(nc, &dimy, &dimx);
  struct ncplane_options nopts = {
    .y = MARGIN * 2 + 2,
    .x = MARGIN * 4 + 2,
    .rows = dimy - MARGIN * 4,
    .cols = dimx - MARGIN * 8,
    .name = "text",
  };
  struct ncplane* n = ncplane_create(stdn, &nopts);
  uint64_t channels = 0;
  ncchannels_set_fg_alpha(&channels, CELL_ALPHA_TRANSPARENT);
  ncchannels_set_bg_alpha(&channels, CELL_ALPHA_TRANSPARENT);
  if(n){
    ncplane_set_base(n, " ", 0, channels);
  }
  return n;
}

static void
usage(FILE* fp){
  fprintf(fp, "usage: textplay [ media ]\n");
}

int main(int argc, char** argv){
  const char* media = NULL;
  if(argc > 3){
    usage(stderr);
    return EXIT_FAILURE;
  }else if(argc == 2){
    media = argv[1];
  }
  struct notcurses* nc = init();
  if(nc == NULL){
    return EXIT_FAILURE;
  }
  struct ncvisual* ncv = NULL;
  if(media){
    notcurses_check_pixel_support(nc);
    if((ncv = ncvisual_from_file(media)) == NULL){
      notcurses_stop(nc);
      return EXIT_FAILURE;
    }
  }
  struct ncplane* tplane = textplane(nc);
  if(tplane == NULL){
    notcurses_stop(nc);
    return EXIT_FAILURE;
  }
  textplay(nc, tplane, ncv);
  ncvisual_destroy(ncv);
  if(notcurses_stop(nc)){
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
