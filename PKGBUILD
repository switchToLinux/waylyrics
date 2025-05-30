# Maintainer: Haden Collins <collinshaden@gmail.com>
pkgname='libwaybar_cffi_lyrics'
pkgver=1.2.2
pkgrel=0
pkgdesc="a cffi module for waybar to get lyrics from various music providers"

arch=('x86_64')
license=("GPL-3.0-or-later")

url="https://github.com/switchToLinux/libwaybar_cffi_lyrics"
source_x86_64=("$pkgname-$pkgver.tar.gz::${url}/releases/download/$pkgver/$pkgname.tar.gz")
sha256sums_x86_64=('d0b8e93c169054d689efedabe64413d4deb0489a2ca1259a8ae50c2c5974dadf')

makedepends=("meson" "git" "ninja")
depends=("gtk3" "libepoxy" "sdbus-cpp" "libcurl-gnutls" "glm")


build() {
    cd $srcdir
    make
}

package() {
    DESTDIR="$pkgdir" make install
    install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
